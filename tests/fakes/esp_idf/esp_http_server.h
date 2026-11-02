/*
 * GS-232B Open - rotator controller firmware.
 * Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef TEST_ESP_HTTP_SERVER_H
#define TEST_ESP_HTTP_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "esp_err.h"

typedef void *httpd_handle_t;
typedef void (*httpd_free_ctx_fn_t)(void *ctx);
typedef void (*httpd_work_fn_t)(void *ctx);
typedef int (*httpd_send_func_t)(httpd_handle_t server, int fd,
    const char *bytes, size_t size, int flags);
typedef struct {
    httpd_handle_t handle;
    int method;
    void *user_ctx, *sess_ctx;
    httpd_free_ctx_fn_t free_ctx;
    void *aux;
} httpd_req_t;

enum { HTTP_GET = 1 };
typedef enum {
    HTTPD_WS_TYPE_CONTINUE = 0, HTTPD_WS_TYPE_TEXT = 1, HTTPD_WS_TYPE_BINARY = 2,
    HTTPD_WS_TYPE_CLOSE = 8, HTTPD_WS_TYPE_PING = 9, HTTPD_WS_TYPE_PONG = 10
} httpd_ws_type_t;
typedef struct {
    bool final, fragmented;
    httpd_ws_type_t type;
    uint8_t *payload;
    size_t len, left_len;
} httpd_ws_frame_t;

typedef struct {
    const char *uri;
    int method;
    esp_err_t (*handler)(httpd_req_t *request);
    void *user_ctx;
    bool is_websocket, handle_ws_control_frames;
    const char *supported_subprotocol;
    esp_err_t (*ws_pre_handshake_cb)(httpd_req_t *request);
    esp_err_t (*ws_post_handshake_cb)(httpd_req_t *request);
} httpd_uri_t;

typedef struct {
    unsigned task_priority;
    uint16_t server_port, max_open_sockets, recv_wait_timeout, send_wait_timeout;
    bool lru_purge_enable, keep_alive_enable;
    int keep_alive_idle, keep_alive_interval, keep_alive_count;
    void *global_user_ctx;
} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() {.server_port = 80}

esp_err_t httpd_start(httpd_handle_t *server, const httpd_config_t *config);
esp_err_t httpd_stop(httpd_handle_t server);
esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *uri);
int httpd_req_to_sockfd(httpd_req_t *request);
void *httpd_sess_get_ctx(httpd_handle_t server, int fd);
esp_err_t httpd_sess_set_send_override(httpd_handle_t server, int fd, httpd_send_func_t send_fn);
esp_err_t httpd_ws_recv_frame_part(httpd_req_t *request, httpd_ws_frame_t *frame, size_t max_len);
esp_err_t httpd_ws_send_frame_async(httpd_handle_t server, int fd, httpd_ws_frame_t *frame);
esp_err_t httpd_queue_work(httpd_handle_t server, httpd_work_fn_t work, void *ctx);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
esp_err_t httpd_resp_send(httpd_req_t *, const char *, ssize_t);
size_t httpd_req_get_hdr_value_len(httpd_req_t *, const char *);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *, const char *, char *, size_t);
#endif
