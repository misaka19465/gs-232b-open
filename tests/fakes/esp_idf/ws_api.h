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

#ifndef TEST_WS_API_H
#define TEST_WS_API_H

#include "esp_http_server.h"
#include "lwip/sockets.h"

#define FAKE_WS_SOCKETS 8U
#define FAKE_WS_WIRE_BYTES 65536U
typedef struct {
    httpd_req_t request;
    httpd_send_func_t send_fn;
    uint32_t local_address;
    int address_family;
    const char *origin;
    char bound_interface[IFNAMSIZ];
    bool native_ipv6;
    bool active, close_queued, bind_error;
    uint8_t input[FAKE_WS_WIRE_BYTES], output[FAKE_WS_WIRE_BYTES], mask[4];
    size_t input_size, input_offset, output_size;
    size_t send_limit, send_calls, receive_calls, largest_read, bind_calls;
    int send_error;
} fake_ws_socket_t;
typedef struct {
    httpd_config_t config;
    httpd_uri_t uri;
    httpd_uri_t manage_uri;
    httpd_uri_t page_uri;
    const char *response_type, *response_body;
    const char *response_encoding;
    ssize_t response_size;
    fake_ws_socket_t sockets[FAKE_WS_SOCKETS];
    httpd_work_fn_t work;
    void *work_ctx;
    void (*control_step)(void *ctx);
    void *control_ctx;
    void (*bind_step)(void *ctx);
    void *bind_ctx;
    uint64_t now_us;
    size_t allocations, live_semaphores, hooks, handlers;
    size_t fail_allocation;
    bool started, page_registered, fail_start, fail_register, fail_stop, fail_queue, fail_override, fail_encode;
} fake_ws_t;
extern fake_ws_t fake_ws;

void fake_ws_reset(void);
esp_err_t fake_ws_connect(int fd, uint32_t local_address);
esp_err_t fake_ws_connect_uri(int fd, uint32_t local_address, const char *uri);
void fake_ws_disconnect(int fd);
void fake_ws_run_work(void);
esp_err_t fake_ws_frame(int fd, httpd_ws_type_t type, bool final,
                         const void *bytes, size_t size);
bool fake_ws_take_frame(int fd, httpd_ws_type_t *type,
                          uint8_t *bytes, size_t capacity, size_t *size);
#endif
