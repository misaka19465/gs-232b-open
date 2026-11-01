#include "ws_api.h"
#include "lwip/sockets.h"
#include "esp_timer.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

fake_ws_t fake_ws;

void fake_ws_reset(void)
{
    assert(fake_ws.live_semaphores == 0 && !fake_ws.started);
    memset(&fake_ws, 0, sizeof(fake_ws));
    for (size_t i = 0; i < FAKE_WS_SOCKETS; ++i) { fake_ws.sockets[i].send_limit = SIZE_MAX; }
}
int64_t esp_timer_get_time(void) { return (int64_t)fake_ws.now_us; }

esp_err_t httpd_start(httpd_handle_t *server, const httpd_config_t *config)
{
    if (fake_ws.fail_start) { return ESP_FAIL; }
    assert(!fake_ws.started);
    fake_ws.config = *config;
    *server = &fake_ws;
    fake_ws.started = true;
    return ESP_OK;
}
esp_err_t httpd_stop(httpd_handle_t server)
{
    assert(server == &fake_ws);
    if (fake_ws.fail_stop) { return ESP_FAIL; }
    /* NOTE: SDK 会 free 默认全局上下文，静态 adapter 只能经借用的 URI ctx 传入。 */
    assert(fake_ws.config.global_user_ctx == NULL);
    fake_ws_run_work();
    for (size_t i = 0; i < FAKE_WS_SOCKETS; ++i) { fake_ws_disconnect((int)i); }
    fake_ws.work = NULL;
    fake_ws.started = false;
    return ESP_OK;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *uri)
{
    assert(server == &fake_ws);
    if (fake_ws.fail_register) { return ESP_FAIL; }
    fake_ws.uri = *uri;
    return ESP_OK;
}
int httpd_req_to_sockfd(httpd_req_t *request)
{
    return (int)((fake_ws_socket_t *)request->aux - fake_ws.sockets);
}
void *httpd_sess_get_ctx(httpd_handle_t server, int fd)
{
    assert(server == &fake_ws && fd >= 0 && fd < (int)FAKE_WS_SOCKETS);
    return fake_ws.sockets[fd].request.sess_ctx;
}
esp_err_t httpd_sess_set_send_override(httpd_handle_t server, int fd, httpd_send_func_t send_fn)
{
    assert(server == &fake_ws);
    if (fake_ws.fail_override) { return ESP_FAIL; }
    fake_ws.sockets[fd].send_fn = send_fn;
    return ESP_OK;
}
esp_err_t fake_ws_connect(int fd, uint32_t local_address)
{
    fake_ws_socket_t *socket = &fake_ws.sockets[fd];
    assert(fake_ws.started && !socket->active);
    socket->local_address = local_address;
    socket->request = (httpd_req_t){.handle = &fake_ws, .method = HTTP_GET,
        .user_ctx = fake_ws.uri.user_ctx, .aux = socket};
    socket->active = true;
    ++fake_ws.hooks;
    esp_err_t error = fake_ws.uri.ws_pre_handshake_cb(&socket->request);
    if (error == ESP_OK) {
        ++fake_ws.hooks;
        error = fake_ws.uri.ws_post_handshake_cb(&socket->request);
    }
    if (error != ESP_OK) { fake_ws_disconnect(fd); }
    return error;
}
void fake_ws_disconnect(int fd)
{
    fake_ws_socket_t *socket = &fake_ws.sockets[fd];
    if (socket->active && socket->request.sess_ctx != NULL) {
        socket->request.free_ctx(socket->request.sess_ctx);
    }
    socket->request.sess_ctx = NULL;
    socket->active = socket->close_queued = false;
    socket->input_size = socket->input_offset = 0;
}
int fake_ws_getsockname(int fd, struct sockaddr *address, socklen_t *length)
{
    fake_ws_socket_t *socket = &fake_ws.sockets[fd];
    if (socket->address_family == AF_INET6) {
        assert(*length >= sizeof(struct sockaddr_in6));
        struct sockaddr_in6 *local = (struct sockaddr_in6 *)address;
        memset(local, 0, sizeof(*local));
        local->sin6_family = AF_INET6;
        uint8_t *octets = local->sin6_addr.s6_addr;
        if (socket->native_ipv6) { octets[0] = 0x20; octets[1] = 0x01; }
        else { octets[10] = octets[11] = 0xff; }
        for (size_t i = 0; i < 4; ++i) {
            octets[12 + i] = (uint8_t)(socket->local_address >> ((3U - i) * 8U));
        }
        *length = sizeof(*local);
        return 0;
    }
    assert(*length >= sizeof(struct sockaddr_in));
    struct sockaddr_in *local = (struct sockaddr_in *)address;
    memset(local, 0, sizeof(*local));
    local->sin_family = AF_INET;
    local->sin_addr.s_addr = htonl(socket->local_address);
    *length = sizeof(*local);
    return 0;
}
ssize_t fake_ws_socket_send(int fd, const void *bytes, size_t size, int flags)
{
    fake_ws_socket_t *socket = &fake_ws.sockets[fd];
    assert(socket->active && flags == MSG_DONTWAIT);
    ++socket->send_calls;
    if (socket->send_error != 0) { errno = socket->send_error; return -1; }
    if (socket->send_limit == 0) { errno = EAGAIN; return -1; }
    if (size > socket->send_limit) { size = socket->send_limit; }
    assert(size <= FAKE_WS_WIRE_BYTES - socket->output_size);
    memcpy(socket->output + socket->output_size, bytes, size);
    socket->output_size += size;
    return (ssize_t)size;
}
esp_err_t httpd_queue_work(httpd_handle_t server, httpd_work_fn_t work, void *ctx)
{
    assert(server == &fake_ws);
    if (fake_ws.fail_queue || fake_ws.work != NULL) { return ESP_FAIL; }
    fake_ws.work = work;
    fake_ws.work_ctx = ctx;
    return ESP_OK;
}
int fake_ws_socket_shutdown(int fd, int how)
{
    assert(how == SHUT_RDWR && fake_ws.sockets[fd].active);
    fake_ws.sockets[fd].close_queued = true;
    return 0;
}
void fake_ws_run_work(void)
{
    if (fake_ws.work != NULL) {
        httpd_work_fn_t work = fake_ws.work;
        fake_ws.work = NULL;
        work(fake_ws.work_ctx);
    }
    for (size_t i = 0; i < FAKE_WS_SOCKETS; ++i) {
        if (fake_ws.sockets[i].close_queued) { fake_ws_disconnect((int)i); }
    }
}

esp_err_t httpd_ws_recv_frame_part(httpd_req_t *request, httpd_ws_frame_t *frame, size_t max_len)
{
    fake_ws_socket_t *socket = request->aux;
    ++socket->receive_calls;
    if (frame->len == 0) {
        /* NOTE: 模拟锁定 SDK 的两阶段读取，包括零长帧再次探测的陷阱和掩码偏移。 */
        if (socket->input_size - socket->input_offset < 6) { return ESP_FAIL; }
        const uint8_t first = socket->input[socket->input_offset++];
        const uint8_t second = socket->input[socket->input_offset++];
        frame->final = (first & 0x80U) != 0;
        frame->type = (httpd_ws_type_t)(first & 0x0fU);
        size_t size = second & 0x7fU;
        if (size == 126) {
            if (socket->input_size - socket->input_offset < 6) { return ESP_FAIL; }
            size = ((size_t)socket->input[socket->input_offset] << 8) |
                socket->input[socket->input_offset + 1];
            socket->input_offset += 2;
        } else if (size == 127) {
            if (socket->input_size - socket->input_offset < 12) { return ESP_FAIL; }
            size = 0;
            for (size_t i = 0; i < 8; ++i) { size = (size << 8) | socket->input[socket->input_offset++]; }
        }
        if ((second & 0x80U) == 0) { return ESP_FAIL; }
        memcpy(socket->mask, socket->input + socket->input_offset, 4);
        socket->input_offset += 4;
        frame->len = frame->left_len = size;
    }
    if (max_len == 0) { return ESP_OK; }
    size_t size = frame->left_len < max_len ? frame->left_len : max_len;
    if (size > socket->input_size - socket->input_offset || frame->payload == NULL) { return ESP_FAIL; }
    if (size > socket->largest_read) { socket->largest_read = size; }
    for (size_t i = 0; i < size; ++i) {
        frame->payload[i] = socket->input[socket->input_offset++] ^
            socket->mask[(frame->len - frame->left_len + i) % 4];
    }
    frame->left_len -= size;
    return ESP_OK;
}

esp_err_t fake_ws_frame(int fd, httpd_ws_type_t type, bool final, const void *bytes, size_t size)
{
    fake_ws_socket_t *socket = &fake_ws.sockets[fd];
    assert(socket->active && size + 14 < sizeof(socket->input));
    const uint8_t mask[] = {0x13, 0xa5, 0xf8, 0x47};
    size_t at = 0;
    socket->input[at++] = (uint8_t)type | (final ? 0x80U : 0);
    if (size < 126) { socket->input[at++] = 0x80U | (uint8_t)size; }
    else {
        socket->input[at++] = 0x80U | 126U;
        socket->input[at++] = (uint8_t)(size >> 8);
        socket->input[at++] = (uint8_t)size;
    }
    memcpy(socket->input + at, mask, sizeof(mask));
    at += sizeof(mask);
    for (size_t i = 0; i < size; ++i) { socket->input[at++] = ((const uint8_t *)bytes)[i] ^ mask[i % 4]; }
    socket->input_size = at;
    socket->input_offset = 0;
    ++fake_ws.handlers;
    const esp_err_t error = fake_ws.uri.handler(&socket->request);
    if (error != ESP_OK) { fake_ws_disconnect(fd); }
    return error;
}

esp_err_t httpd_ws_send_frame_async(httpd_handle_t server, int fd, httpd_ws_frame_t *frame)
{
    if (fake_ws.fail_encode) { return ESP_FAIL; }
    fake_ws_socket_t *socket = &fake_ws.sockets[fd];
    uint8_t header[4] = {(uint8_t)(frame->type |
        ((!frame->fragmented || frame->final) ? 0x80U : 0)), 0, 0, 0};
    size_t size = 2;
    if (frame->len < 126) { header[1] = (uint8_t)frame->len; }
    else {
        header[1] = 126;
        header[2] = (uint8_t)(frame->len >> 8);
        header[3] = (uint8_t)frame->len;
        size = 4;
    }
    if (socket->send_fn(server, fd, (const char *)header, size, 0) < 0) { return ESP_FAIL; }
    if (frame->len != 0 && socket->send_fn(server, fd,
        (const char *)frame->payload, frame->len, 0) < 0) { return ESP_FAIL; }
    return ESP_OK;
}

bool fake_ws_take_frame(int fd, httpd_ws_type_t *type,
                          uint8_t *bytes, size_t capacity, size_t *size)
{
    fake_ws_socket_t *socket = &fake_ws.sockets[fd];
    *size = 0;
    if (socket->output_size < 2) { return false; }
    assert((socket->output[0] & 0x80U) != 0 && (socket->output[1] & 0x80U) == 0);
    size_t length = socket->output[1], header = 2;
    if (length == 126) {
        if (socket->output_size < 4) { return false; }
        length = ((size_t)socket->output[2] << 8) | socket->output[3];
        header = 4;
    }
    if (socket->output_size < header + length) { return false; }
    assert(capacity >= length);
    *type = (httpd_ws_type_t)(socket->output[0] & 0x0fU);
    memcpy(bytes, socket->output + header, length);
    *size = length;
    memmove(socket->output, socket->output + header + length, socket->output_size - header - length);
    socket->output_size -= header + length;
    return true;
}
