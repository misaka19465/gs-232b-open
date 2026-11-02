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

#define WS_AP_IP UINT32_C(0xc0a80401)

static bool connect_management(ws_fixture_t *fixture, int fd, uint32_t ip)
{
    CHECK(fake_ws_connect_uri(fd, ip, GS232B_WS_MANAGE_URI) == ESP_OK);
    fake_ws_run_work();
    CHECK(ws_expect_bytes(fd, "Connect OK\r\n"));
    CHECK(ws_client(fixture, fd)->management);
    const gs232b_session_key_t key = ws_client(fixture, fd)->session;
    CHECK(fixture->dispatcher.control.sessions[key.session_id - 1U].management_only);
    return true;
}

static bool extension_command(ws_fixture_t *fixture, int fd, const char *line,
    gs232b_error_t expected)
{
    CHECK(fake_ws_frame(fd, HTTPD_WS_TYPE_TEXT, true, line, strlen(line)) == ESP_OK);
    const gs232b_error_t admitted = gs232b_esp_ws_service(&fixture->ws);
    CHECK(admitted == GS232B_OK || admitted == expected);
    const gs232b_error_t executed = gs232b_dispatcher_tick(&fixture->dispatcher);
    CHECK(executed == GS232B_OK || executed == expected);
    fake_ws_run_work();
    CHECK(ws_poll(fixture, 12));
    uint8_t bytes[4096]; size_t used = 0, size = 0;
    httpd_ws_type_t type;
    while (fake_ws_take_frame(fd, &type, bytes + used, sizeof(bytes) - used, &size)) {
        CHECK(type == HTTPD_WS_TYPE_BINARY);
        used += size;
    }
    CHECK(used >= 4 && bytes[0] == '!' && bytes[used - 2U] == '\r' && bytes[used - 1U] == '\n');
    gs232b_json_value_t json, ok;
    CHECK(gs232b_json_parse((gs232b_byte_span_t){bytes + 1, used - 3U}, &json) == GS232B_OK);
    CHECK(gs232b_json_get(json, "ok", &ok) == GS232B_OK);
    CHECK(ok.raw.size == (expected == GS232B_OK ? 4U : 5U));
    if (expected != GS232B_OK) {
        gs232b_json_value_t error;
        CHECK(gs232b_json_get(json, "error", &error) == GS232B_OK);
        uint8_t name[64]; size_t length = 0;
        CHECK(gs232b_json_string_decode(error, name, sizeof(name), &length) == GS232B_OK);
        CHECK(length == strlen(gs232b_error_name(expected)));
        CHECK(memcmp(name, gs232b_error_name(expected), length) == 0);
    }
    return true;
}

static bool management_token(ws_fixture_t *fixture, char token[65])
{
    gs232b_extension_request_t request;
    CHECK(gs232b_extension_parse((const uint8_t *)"!ADMIN ENABLE", 13, &request) == GS232B_OK);
    request.transport = GS232B_TRANSPORT_UART1;
    uint8_t data[GS232B_EXTENSION_DATA_MAX_BYTES];
    gs232b_extension_response_t response;
    CHECK(gs232b_extension_dispatch(&fixture->dispatcher.extensions, &request, data,
        sizeof(data), &response) == GS232B_OK);
    gs232b_json_value_t root, value;
    CHECK(gs232b_json_parse(response.data, &root) == GS232B_OK);
    CHECK(gs232b_json_get(root, "token", &value) == GS232B_OK);
    size_t token_size = 0;
    CHECK(gs232b_json_string_decode(value, (uint8_t *)token, 64, &token_size) == GS232B_OK && token_size == 64);
    token[64] = '\0';
    return true;
}

static bool ap_uri_and_command_boundaries(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(strcmp(fake_ws.manage_uri.uri, GS232B_WS_MANAGE_URI) == 0);
    CHECK(gs232b_esp_ws_interface(&f.ws, GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS,
        WS_AP_IP, 1, "ap0") == GS232B_OK);
    CHECK(fake_ws_connect(0, WS_AP_IP) == ESP_FAIL);
    CHECK(fake_ws_connect_uri(0, WS_AP_IP, GS232B_WS_MANAGE_URI) == ESP_FAIL);
    CHECK(ws_poll(&f, 2));
    CHECK(gs232b_provisioning_start(&f.dispatcher.provisioning, 0) == GS232B_OK);
    CHECK(connect_management(&f, 0, WS_AP_IP));
    CHECK(strcmp(fake_ws.sockets[0].bound_interface, "ap0") == 0);
    CHECK(extension_command(&f, 0, "!SYS INFO\r", GS232B_OK));
    CHECK(extension_command(&f, 0, "!NET GET\r", GS232B_OK));
    const size_t attempts = f.sim.output_attempts;
    const gs232b_motion_config_t mechanical = f.sim.motion.config;
    static const char *const gs_lines[] = {
        "R\r", "L\r", "U\r", "D\r", "M180\r", "W180 090\r", "T\r", "X4\r",
        "O\r", "O2\r", "F\r", "F2\r", "Y\r", "YES\r", "NO\r", "P36\r", "Z\r", "S\r"
    };
    for (size_t i = 0; i < sizeof(gs_lines) / sizeof(gs_lines[0]); ++i) {
        CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, true, gs_lines[i], strlen(gs_lines[i])) == ESP_OK);
        CHECK(gs232b_esp_ws_service(&f.ws) == GS232B_ERROR_NOT_AUTHORIZED);
        CHECK(ws_poll(&f, 3));
        CHECK(ws_expect_bytes(0, "?>\r\n"));
    }
    static const char *const forbidden[] = {
        "!OWNER GET\r", "!OWNER TAKE\r", "!OWNER RELEASE\r", "!MOTION STATUS\r", "!FAULT CLEAR\r",
        "!SESSION WATCHDOG {\"timeout_ms\":1000}\r", "!SESSION PING\r", "!ADMIN ENABLE\r",
        "!NET RESET {\"confirm\":\"NETWORK_ONLY\"}\r", "!PROV START\r"
    };
    for (size_t i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
        CHECK(extension_command(&f, 0, forbidden[i], GS232B_ERROR_NOT_AUTHORIZED));
    }
    CHECK(f.sim.output_attempts == attempts);
    CHECK(memcmp(&mechanical, &f.sim.motion.config, sizeof(mechanical)) == 0);
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(extension_command(&f, 0, "!CFG BEGIN\r", GS232B_OK));
    CHECK(extension_command(&f, 0, "!NET SET {\"wifi\":{\"ssid\":\"via-ap\"}}\r", GS232B_OK));
    CHECK(strcmp(f.dispatcher.network_config.candidate.wifi.ssid, "via-ap") == 0);
    static const char expired[] = "!NET SET {\"wifi\":{\"ssid\":\"expired\"}}\r";
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, true, expired, sizeof(expired) - 1U) == ESP_OK);
    CHECK(gs232b_esp_ws_service(&f.ws) == GS232B_OK);
    f.sim.now_us = f.dispatcher.provisioning.status.deadline_us;
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_ERROR_NOT_AUTHORIZED);
    CHECK(strcmp(f.dispatcher.network_config.active.wifi.ssid, "") == 0);
    CHECK(ws_poll(&f, 14));
    CHECK(!fake_ws.sockets[0].active);
    CHECK(f.dispatcher.network_config.candidate_id == 0);
    return ws_close(&f);
}

static bool ethernet_management_extensions_and_control_separation(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(connect_management(&f, 0, WS_ETH_IP));
    CHECK(extension_command(&f, 0, "!SYS INFO\r", GS232B_OK));
    CHECK(extension_command(&f, 0, "!OWNER GET\r", GS232B_OK));
    CHECK(extension_command(&f, 0, "!OWNER TAKE\r", GS232B_OK));
    CHECK(extension_command(&f, 0, "!SESSION WATCHDOG {\"timeout_ms\":6000}\r", GS232B_OK));
    CHECK(extension_command(&f, 0, "!SESSION PING\r", GS232B_OK));
    CHECK(extension_command(&f, 0, "!OWNER RELEASE\r", GS232B_OK));
    CHECK(extension_command(&f, 0, "!MOTION STATUS\r", GS232B_OK));
    CHECK(extension_command(&f, 0, "!CFG BEGIN\r", GS232B_ERROR_NOT_AUTHORIZED));
    char token[65], command[128];
    CHECK(management_token(&f, token));
    snprintf(command, sizeof(command), "!SESSION AUTH {\"token\":\"%s\"}\r", token);
    CHECK(extension_command(&f, 0, command, GS232B_OK));
    CHECK(extension_command(&f, 0, "!CFG BEGIN\r", GS232B_OK));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, true, "R\r", 2) == ESP_OK);
    CHECK(gs232b_esp_ws_service(&f.ws) == GS232B_ERROR_NOT_AUTHORIZED);
    CHECK(ws_poll(&f, 3));
    CHECK(ws_expect_bytes(0, "?>\r\n"));
    CHECK(stopped(&f.sim));
    CHECK(ws_connect(&f, 1, WS_STA_IP));
    CHECK(ws_command(&f, 1, "R\r", "\r"));
    fake_ws_disconnect(0);
    CHECK(ws_poll(&f, 3));
    CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, ws_client(&f, 1)->session));
    CHECK(ws_command(&f, 1, "S\r", "\r"));
    return ws_close(&f);
}

static bool origin_policy(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    fake_ws.sockets[0].origin = "http://attacker.invalid";
    CHECK(fake_ws_connect_uri(0, WS_ETH_IP, GS232B_WS_MANAGE_URI) == ESP_FAIL);
    fake_ws.sockets[0].origin = "null";
    CHECK(fake_ws_connect(0, WS_ETH_IP) == ESP_FAIL);
    fake_ws.sockets[0].origin = "http://192.168.1.2:80";
    CHECK(connect_management(&f, 0, WS_ETH_IP));
    fake_ws.sockets[1].origin = "http://192.168.2.2";
    CHECK(ws_connect(&f, 1, WS_STA_IP));
    return ws_close(&f);
}

bool ws_management_tests(void)
{
    return ap_uri_and_command_boundaries() && ethernet_management_extensions_and_control_separation() && origin_policy();
}
