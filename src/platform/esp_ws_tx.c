#include "esp_ws_internal.h"

#include <errno.h>
#include <string.h>
#include "esp_timer.h"
#include "lwip/sockets.h"

int gs232b_ws_encode_send(httpd_handle_t server, int fd, const char *bytes,
                           size_t size, int flags)
{
    (void)flags;
    gs232b_ws_client_t *client = httpd_sess_get_ctx(server, fd);
    if (client == NULL || size > sizeof(client->wire) - client->wire_size) { return -1; }
    memcpy(client->wire + client->wire_size, bytes, size);
    client->wire_size += size;
    return (int)size;
}

bool gs232b_ws_control(gs232b_ws_client_t *client, httpd_ws_type_t type,
                        const uint8_t *bytes, size_t size)
{
    if (size > 125U || client->control_count == GS232B_WS_CONTROL_PACKETS) { return false; }
    gs232b_ws_control_t *control = &client->controls[
        (client->control_head + client->control_count) % GS232B_WS_CONTROL_PACKETS];
    control->type = type;
    control->size = size;
    if (size != 0) { memcpy(control->bytes, bytes, size); }
    ++client->control_count;
    return true;
}

static bool encode(gs232b_ws_client_t *client, httpd_ws_type_t type,
                     uint8_t *bytes, size_t size)
{
    client->wire_size = client->wire_sent = 0;
    httpd_ws_frame_t frame = {.final = true, .type = type, .payload = bytes, .len = size};
    return httpd_ws_send_frame_async(client->adapter->server, client->fd, &frame) == ESP_OK;
}

void gs232b_ws_fail(gs232b_ws_client_t *client, uint16_t code)
{
    atomic_store(&client->close_requested, true);
    /* NOTE: 已发送半个帧时不能插入 Close，否则破坏 TCP 上尚未完成的帧。
     * 完整空闲边界上尽力发送一次，失败/短写直接断开，不循环等待。 */
    if (client->wire_sent == client->wire_size) {
        uint8_t bytes[] = {(uint8_t)(code >> 8), (uint8_t)code};
        if (encode(client, HTTPD_WS_TYPE_CLOSE, bytes, sizeof(bytes))) {
            (void)send(client->fd, client->wire, client->wire_size, MSG_DONTWAIT);
        }
    }
}

static bool heartbeat(gs232b_ws_client_t *client, gs232b_time_us_t now)
{
    if (now < client->last_pong_us || now - client->last_pong_us >= GS232B_WS_PONG_TIMEOUT_US) {
        gs232b_ws_fail(client, 1001);
        return false;
    }
    if (now >= client->last_ping_us && now - client->last_ping_us >= GS232B_WS_PING_US) {
        if (!client->awaiting_pong) { ++client->ping_serial; }
        const uint32_t serial = client->ping_serial;
        const uint8_t bytes[] = {(uint8_t)(serial >> 24), (uint8_t)(serial >> 16),
            (uint8_t)(serial >> 8), (uint8_t)serial};
        if (!gs232b_ws_control(client, HTTPD_WS_TYPE_PING, bytes, sizeof(bytes))) {
            gs232b_ws_fail(client, 1008);
            return false;
        }
        client->awaiting_pong = true;
        client->last_ping_us = now;
    }
    return true;
}

static bool prepare(gs232b_ws_client_t *client)
{
    if (client->control_count != 0) {
        gs232b_ws_control_t *control = &client->controls[client->control_head];
        if (!encode(client, control->type, control->bytes, control->size)) { return false; }
        client->control_head = (client->control_head + 1U) % GS232B_WS_CONTROL_PACKETS;
        --client->control_count;
        return true;
    }
    gs232b_esp_ws_t *adapter = client->adapter;
    uint8_t bytes[GS232B_WS_CHUNK_BYTES];
    size_t size = 0;
    if (xSemaphoreTake(adapter->lock, 0) != pdPASS) { return true; }
    size = client->tx_count < sizeof(bytes) ? client->tx_count : sizeof(bytes);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = client->tx[(client->tx_head + i) % GS232B_WS_TX_BYTES];
    }
    client->tx_head = (client->tx_head + size) % GS232B_WS_TX_BYTES;
    client->tx_count -= size;
    if (size != 0) {
        atomic_store(&client->send_started_ms, (uint32_t)((uint64_t)esp_timer_get_time() / 1000U));
        atomic_store(&client->send_pending, true);
    } else {
        /* NOTE: 上轮完成发送时若抢锁失败，本轮在空 FIFO 下补交完成通知。 */
        atomic_store(&client->send_pending, false);
    }
    (void)xSemaphoreGive(adapter->lock);
    return size == 0 || encode(client, HTTPD_WS_TYPE_BINARY, bytes, size);
}

static void transmit(gs232b_ws_client_t *client, gs232b_time_us_t now)
{
    if (client->wire_sent == client->wire_size) {
        client->wire_size = client->wire_sent = 0;
        if (!prepare(client)) { gs232b_ws_fail(client, 1011); return; }
        client->wire_started_us = now;
    }
    if (client->wire_size == 0) { return; }
    if (now < client->wire_started_us || now - client->wire_started_us >= GS232B_WS_SEND_TIMEOUT_US) {
        gs232b_ws_fail(client, 1008);
        return;
    }
    const size_t remaining = client->wire_size - client->wire_sent;
    const int sent = send(client->fd, client->wire + client->wire_sent, remaining, MSG_DONTWAIT);
    if (sent > 0 && (size_t)sent <= remaining) {
        client->wire_sent += (size_t)sent;
        if (client->wire_sent == client->wire_size &&
            xSemaphoreTake(client->adapter->lock, 0) == pdPASS) {
            /* NOTE: 与 send_bytes 的入队在同一把锁下发布，避免吞掉新入队通知。 */
            atomic_store(&client->send_pending, client->tx_count != 0);
            if (client->tx_count != 0) {
                atomic_store(&client->send_started_ms, (uint32_t)(now / 1000U));
            }
            (void)xSemaphoreGive(client->adapter->lock);
        }
    } else if (sent == 0 || sent > 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        gs232b_ws_fail(client, 1011);
    }
}

void gs232b_ws_pump(void *ctx)
{
    gs232b_esp_ws_t *adapter = ctx;
    const gs232b_time_us_t now = (uint64_t)esp_timer_get_time();
    for (size_t i = 0; i < GS232B_WS_CLIENTS; ++i) {
        gs232b_ws_client_t *client = &adapter->clients[i];
        if (!atomic_load(&client->occupied) || atomic_load(&client->http_closed)) { continue; }
        if (!atomic_load(&client->close_requested) && atomic_load(&client->session_ready) &&
            heartbeat(client, now)) { transmit(client, now); }
        if (atomic_load(&client->close_requested) && !client->shutdown_sent) {
            /* NOTE: shutdown 不释放 fd，EOF 由 SDK 当前会话路径关闭。避免 SDK
             * trigger_close 排队的内部 slot 指针在读失败/accept 复用后误关新 socket。 */
            client->shutdown_sent = shutdown(client->fd, SHUT_RDWR) == 0;
        }
    }
    atomic_store(&adapter->pump_pending, false);
}
