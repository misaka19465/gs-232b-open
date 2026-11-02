#include "esp_ws_internal.h"

#include <string.h>
#include <stdio.h>
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

_Static_assert(GS232B_WS_INTERFACE_NAME_BYTES == IFNAMSIZ, "Revalidate lwIP interface name size");

static bool origin_allowed(httpd_req_t *request, uint32_t address)
{
    const size_t size = httpd_req_get_hdr_value_len(request, "Origin");
    if (size == 0) return true;
    char origin[64], expected[48];
    if (size >= sizeof(origin) ||
        httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof(origin)) != ESP_OK) return false;
    const int count = snprintf(expected, sizeof(expected), "http://%u.%u.%u.%u",
        (unsigned)(address >> 24), (unsigned)((address >> 16) & 255U),
        (unsigned)((address >> 8) & 255U), (unsigned)(address & 255U));
    if (count < 0 || (size_t)count + 3U >= sizeof(expected)) return false;
    if (strcmp(origin, expected) == 0) return true;
    memcpy(expected + count, ":80", 4);
    return strcmp(origin, expected) == 0;
}

void gs232b_ws_disconnected(void *ctx)
{
    gs232b_ws_client_t *client = ctx;
    atomic_store(&client->http_closed, true);
}

esp_err_t gs232b_ws_pre_handshake(httpd_req_t *request)
{
    gs232b_ws_uri_context_t *uri = request->user_ctx;
    gs232b_esp_ws_t *adapter = uri->adapter;
    struct sockaddr_storage local;
    socklen_t length = sizeof(local);
    const int fd = httpd_req_to_sockfd(request);
    if (getsockname(fd, (struct sockaddr *)&local, &length) != 0 ||
        xSemaphoreTake(adapter->lock, 0) != pdPASS) {
        return ESP_FAIL;
    }
    uint32_t address = 0;
    if (local.ss_family == AF_INET) {
        address = ntohl(((const struct sockaddr_in *)&local)->sin_addr.s_addr);
    }
#if CONFIG_LWIP_IPV6
    else if (local.ss_family == AF_INET6) {
        const struct in6_addr *v6 = &((const struct sockaddr_in6 *)&local)->sin6_addr;
        /* NOTE: HTTPD 6.1 默认双栈监听；只把 IPv4 映射地址关联至 ETH/STA 快照。 */
        if (IN6_IS_ADDR_V4MAPPED(v6)) {
            const uint8_t *octets = (const uint8_t *)v6;
            address = ((uint32_t)octets[12] << 24) | ((uint32_t)octets[13] << 16) |
                ((uint32_t)octets[14] << 8) | octets[15];
        }
    }
#endif
    int interface = -1;
    gs232b_ws_interface_t snapshot = {0};
    gs232b_ws_client_t *client = NULL;
    for (size_t i = 0; i < 3; ++i) {
        if (address != 0 && adapter->interfaces[i].address == address) {
            if (interface != -1) { interface = -1; break; }
            interface = (int)i;
        }
    }
    if (interface != -1 && (uri->management ||
        interface != (int)(GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS - GS232B_TRANSPORT_ETHERNET_WS))) {
        for (size_t i = 0; i < GS232B_WS_CLIENTS; ++i) {
            if (!atomic_load(&adapter->clients[i].occupied)) {
                client = &adapter->clients[i];
                snapshot = adapter->interfaces[interface];
                break;
            }
        }
    }
    (void)xSemaphoreGive(adapter->lock);
    if (client == NULL || !origin_allowed(request, address)) { return ESP_FAIL; }

    struct ifreq binding = {0};
    size_t name_length = 0;
    while (name_length < sizeof(snapshot.if_name) && snapshot.if_name[name_length] != '\0') {
        ++name_length;
    }
    if (name_length == 0 || name_length == sizeof(snapshot.if_name) ||
        name_length >= sizeof(binding.ifr_name)) { return ESP_FAIL; }
    memcpy(binding.ifr_name, snapshot.if_name, name_length + 1U);
    /* NOTE: socket 选项可能等待 TCP/IP 任务，不持有控制任务共享锁；绑定后重新验证
     * 两个接口的归属，避免绑定期间地址变更或重名地址被错误接纳。 */
    if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &binding, sizeof(binding)) != 0) {
        return ESP_FAIL;
    }

    if (xSemaphoreTake(adapter->lock, 0) != pdPASS) { return ESP_FAIL; }
    const gs232b_ws_interface_t current = adapter->interfaces[interface];
    const bool duplicate = (interface != 0 && adapter->interfaces[0].address == snapshot.address) ||
        (interface != 1 && adapter->interfaces[1].address == snapshot.address) ||
        (interface != 2 && adapter->interfaces[2].address == snapshot.address);
    if (current.address != snapshot.address || current.epoch != snapshot.epoch ||
        strcmp(current.if_name, snapshot.if_name) != 0 ||
        duplicate ||
        atomic_load(&client->occupied)) {
        (void)xSemaphoreGive(adapter->lock);
        return ESP_FAIL;
    }
    (void)xSemaphoreTake(client->ready, 0);
    client->fd = fd;
    client->interface = snapshot;
    client->transport = (gs232b_transport_t)(GS232B_TRANSPORT_ETHERNET_WS + interface);
    client->management = uri->management;
    client->message_size = client->control_head = client->control_count = 0;
    client->wire_size = client->wire_sent = 0;
    client->fragmented = client->awaiting_pong = client->shutdown_sent = false;
    client->ping_serial = 0;
    client->last_ping_us = client->last_pong_us = (uint64_t)esp_timer_get_time();
    atomic_store(&client->transport_seen_ms, (uint32_t)(client->last_pong_us / 1000U));
    atomic_store(&client->session_ready, false);
    atomic_store(&client->close_requested, false);
    atomic_store(&client->http_closed, false);
    atomic_store(&client->pending, false);
    atomic_store(&client->send_pending, false);
    atomic_store(&client->occupied, true);
    (void)xSemaphoreGive(adapter->lock);

    request->sess_ctx = client;
    request->free_ctx = gs232b_ws_disconnected;
    return ESP_OK;
}

esp_err_t gs232b_ws_post_handshake(httpd_req_t *request)
{
    gs232b_ws_client_t *client = request->sess_ctx;
    if (httpd_sess_set_send_override(request->handle, client->fd,
                                     gs232b_ws_encode_send) != ESP_OK) { return ESP_FAIL; }
    atomic_store(&client->pending, true);
    /* NOTE: 只等待控制任务建立会话；运动循环不等待 HTTP 回调或 socket。
     * 客户端可在 Upgrade 后立即发送，HTTPD 返回后按顺序读取其缓冲数据。 */
    if (xSemaphoreTake(client->ready, pdMS_TO_TICKS(100)) != pdPASS ||
        !atomic_load(&client->session_ready) || atomic_load(&client->close_requested)) {
        atomic_store(&client->close_requested, true);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool data_frame(gs232b_ws_client_t *client, const httpd_ws_frame_t *frame)
{
    if (frame->len > GS232B_WS_MESSAGE_BYTES) { gs232b_ws_fail(client, 1009); return false; }
    if (frame->type == HTTPD_WS_TYPE_CONTINUE) {
        if (!client->fragmented) { gs232b_ws_fail(client, 1002); return false; }
    } else if (frame->type == HTTPD_WS_TYPE_TEXT || frame->type == HTTPD_WS_TYPE_BINARY) {
        if (client->fragmented) { gs232b_ws_fail(client, 1002); return false; }
        client->message_size = 0;
    } else {
        gs232b_ws_fail(client, 1002);
        return false;
    }
    if (frame->len > GS232B_WS_MESSAGE_BYTES - client->message_size) {
        gs232b_ws_fail(client, 1009);
        return false;
    }
    client->message_size += frame->len;
    client->fragmented = !frame->final;
    return true;
}

static esp_err_t control_frame(gs232b_ws_client_t *client, httpd_req_t *request,
                                httpd_ws_frame_t frame)
{
    uint8_t bytes[125];
    if (!frame.final || frame.len > sizeof(bytes) ||
        (frame.type == HTTPD_WS_TYPE_CLOSE && frame.len == 1)) {
        gs232b_ws_fail(client, 1002);
        return ESP_FAIL;
    }
    frame.payload = bytes;
    /* NOTE: 零长探测已消费帧头；再次用 len=0 调 SDK 会误读下一帧，必须跳过。 */
    if (frame.len != 0 && httpd_ws_recv_frame_part(request, &frame, sizeof(bytes)) != ESP_OK) {
        gs232b_ws_fail(client, 1011);
        return ESP_FAIL;
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        gs232b_ws_fail(client, 1000);
        return ESP_FAIL;
    }
    if (frame.type == HTTPD_WS_TYPE_PING) {
        if (!gs232b_ws_control(client, HTTPD_WS_TYPE_PONG, bytes, frame.len)) {
            gs232b_ws_fail(client, 1008);
            return ESP_FAIL;
        }
    } else if (frame.type == HTTPD_WS_TYPE_PONG) {
        const uint32_t serial = client->ping_serial;
        const uint8_t expected[] = {(uint8_t)(serial >> 24), (uint8_t)(serial >> 16),
            (uint8_t)(serial >> 8), (uint8_t)serial};
        if (client->awaiting_pong && frame.len == sizeof(expected) &&
            memcmp(bytes, expected, sizeof(expected)) == 0) {
            client->last_pong_us = (uint64_t)esp_timer_get_time();
            atomic_store(&client->transport_seen_ms, (uint32_t)(client->last_pong_us / 1000U));
            client->awaiting_pong = false;
        }
    } else {
        gs232b_ws_fail(client, 1002);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t gs232b_ws_receive(httpd_req_t *request)
{
    gs232b_ws_client_t *client = request->sess_ctx;
    if (client == NULL || !atomic_load(&client->session_ready) ||
        atomic_load(&client->close_requested)) { return ESP_FAIL; }
    gs232b_esp_ws_t *adapter = client->adapter;
    gs232b_ws_packet_t packet = {0};
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    packet.context = client->rx_context;
    (void)xSemaphoreGive(adapter->lock);
    httpd_ws_frame_t frame = {0};
    if (httpd_ws_recv_frame_part(request, &frame, 0) != ESP_OK) {
        gs232b_ws_fail(client, 1011);
        return ESP_FAIL;
    }
    if (frame.type >= HTTPD_WS_TYPE_CLOSE) { return control_frame(client, request, frame); }
    if (!data_frame(client, &frame)) { return ESP_FAIL; }
    frame.payload = packet.bytes;
    while (frame.left_len != 0) {
        if (atomic_load(&client->close_requested)) { return ESP_FAIL; }
        const size_t before = frame.left_len;
        if (httpd_ws_recv_frame_part(request, &frame, sizeof(packet.bytes)) != ESP_OK ||
            frame.left_len >= before || before - frame.left_len > sizeof(packet.bytes)) {
            gs232b_ws_fail(client, 1011);
            return ESP_FAIL;
        }
        packet.size = before - frame.left_len;
        (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
        const bool fits = client->rx_count < GS232B_WS_RX_PACKETS;
        if (fits) {
            client->rx[(client->rx_head + client->rx_count) % GS232B_WS_RX_PACKETS] = packet;
            ++client->rx_count;
        }
        (void)xSemaphoreGive(adapter->lock);
        if (!fits) { gs232b_ws_fail(client, 1008); return ESP_FAIL; }
    }
    return ESP_OK;
}
