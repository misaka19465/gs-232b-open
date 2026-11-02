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

#include "ws_test_support.h"

static bool frame_and_message_boundaries(void)
{
    ws_fixture_t f;
    uint8_t bytes[GS232B_WS_MESSAGE_BYTES + 1];
    memset(bytes, ' ', sizeof(bytes));
    bytes[0] = 'C';
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, true, bytes, 16384) == ESP_OK);
    CHECK(ws_client(&f, 0)->rx_count == 64);
    CHECK(ws_poll(&f, 64));
    CHECK(ws_expect_bytes(0, ""));
    CHECK(ws_command(&f, 0, "\r", "AZ=090\r\n"));
    CHECK(fake_ws.sockets[0].largest_read == 256);
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, false, bytes, 8192) == ESP_OK);
    CHECK(ws_poll(&f, 32));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_CONTINUE, true, bytes + 8192, 8192) == ESP_OK);
    CHECK(ws_poll(&f, 32));
    CHECK(ws_command(&f, 0, "\r", "AZ=090\r\n"));
    const size_t reads = fake_ws.sockets[0].receive_calls;
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, true, bytes, 16385) == ESP_FAIL);
    CHECK(fake_ws.sockets[0].receive_calls == reads + 1);
    CHECK(ws_poll(&f, 1));
    const uint8_t too_large[] = {3, 241};
    CHECK(ws_expect(0, HTTPD_WS_TYPE_CLOSE, too_large, sizeof(too_large)));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, false, bytes, 8192) == ESP_OK);
    CHECK(ws_poll(&f, 32));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_CONTINUE, true, bytes + 8192, 8193) == ESP_FAIL);
    CHECK(ws_poll(&f, 1));
    CHECK(ws_expect(0, HTTPD_WS_TYPE_CLOSE, too_large, sizeof(too_large)));
    return ws_close(&f);
}

static bool maximum_gs_upload_and_line_overflow(void)
{
    ws_fixture_t f;
    char bytes[GS232B_WS_MESSAGE_BYTES + 1];
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    size_t size = 4;
    memcpy(bytes, "M010", size);
    for (size_t i = 0; i < 3800; ++i) { memcpy(bytes + size, " 090", 4); size += 4; }
    bytes[size++] = '\r';
    CHECK(size == 15205);
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, true, bytes, size) == ESP_OK);
    CHECK(ws_poll(&f, 64));
    CHECK(ws_expect_bytes(0, "\r"));
    CHECK(f.dispatcher.control.sequence_state.point_count == 3800);
    CHECK(ws_command(&f, 0, "S\r", "\r"));
    memset(bytes, ' ', 16384);
    bytes[0] = 'C';
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, true, bytes, 16384) == ESP_OK);
    CHECK(ws_poll(&f, 64));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, true, " \rC\r", 4) == ESP_OK);
    CHECK(gs232b_esp_ws_service(&f.ws) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(ws_poll(&f, 8));
    CHECK(ws_expect_bytes(0, "?>\r\nAZ=090\r\n"));
    CHECK(fake_ws.sockets[0].active);
    return ws_close(&f);
}

bool ws_boundary_tests(void)
{
    return frame_and_message_boundaries() && maximum_gs_upload_and_line_overflow();
}
