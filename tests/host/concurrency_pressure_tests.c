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

#include "concurrency_test_support.h"

static bool full_live_queue_stop(void)
{
    concurrency_fixture_t f;
    const char queries[] = "C\rC\rC\rC\rC\rC\rC\rC\r";
    const char responses[] = "AZ=090\r\nAZ=090\r\nAZ=090\r\nAZ=090\r\n"
        "AZ=090\r\nAZ=090\r\nAZ=090\r\nAZ=090\r\n";
    CHECK(concurrency_init(&f));
    CHECK(concurrency_net_command(&f, 0, "R\r", "\r"));
    for (int fd = 0; fd < (int)GS232B_WS_CLIENTS; ++fd) {
        CHECK(fake_ws_frame(fd, HTTPD_WS_TYPE_BINARY, true, queries, sizeof(queries) - 1) == ESP_OK);
        CHECK(gs232b_esp_ws_service(&f.network.ws) == GS232B_OK);
    }
    CHECK(concurrency_uart_input(&f, queries));
    CHECK(f.network.dispatcher.control.request_count == 5U * GS232B_CONTROL_REQUESTS_PER_SESSION);
    const gs232b_command_epoch_t epoch = f.network.dispatcher.control.epoch;
    CHECK(concurrency_uart_input(&f, "S\r"));
    CHECK(f.network.dispatcher.control.request_count == 40);
    CHECK(gs232b_dispatcher_tick(&f.network.dispatcher) == GS232B_OK);
    CHECK(stopped(&f.network.sim));
    CHECK(f.network.dispatcher.control.epoch > epoch);
    CHECK(f.network.dispatcher.control.request_count == 0);
    CHECK(f.network.dispatcher.control.request_bytes_used == 0);
    CHECK(concurrency_poll(&f, 8));
    for (int fd = 0; fd < (int)GS232B_WS_CLIENTS; ++fd) CHECK(concurrency_net_expect(fd, responses));
    char uart_responses[sizeof(responses) + 1];
    snprintf(uart_responses, sizeof(uart_responses), "%s\r", responses);
    CHECK(concurrency_uart_expect(uart_responses));
    return concurrency_close(&f);
}

static bool query_flood_and_connection_exhaustion(void)
{
    concurrency_fixture_t f;
    uint8_t flood[GS232B_CONTROL_FEED_MAX_BYTES];
    CHECK(concurrency_init(&f));
    CHECK(concurrency_uart_input(&f, "R\r"));
    CHECK(concurrency_poll(&f, 1));
    CHECK(concurrency_uart_expect("\r"));
    for (int attempt = 0; attempt < 64; ++attempt) {
        CHECK(fake_ws_connect(4, WS_ETH_IP) == ESP_FAIL);
        CHECK(concurrency_poll(&f, 1));
        CHECK(gs232b_session_key_equal(f.network.dispatcher.control.owner, f.uart.session));
    }
    for (size_t i = 0; i < sizeof(flood); i += 2) memcpy(flood + i, "C\r", 2);
    CHECK(fake_ws_frame(1, HTTPD_WS_TYPE_BINARY, true, flood, sizeof(flood)) == ESP_OK);
    CHECK(gs232b_esp_ws_service(&f.network.ws) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(concurrency_bounds(&f));
    CHECK(concurrency_poll(&f, 2));
    CHECK(!fake_ws.sockets[1].active);
    CHECK(gs232b_session_key_equal(f.network.dispatcher.control.owner, f.uart.session));
    CHECK(concurrency_net_command(&f, 2, "C\r", "AZ=090\r\n"));
    CHECK(concurrency_uart_input(&f, "S\r"));
    CHECK(concurrency_poll(&f, 2));
    CHECK(concurrency_uart_expect("\r"));
    CHECK(ws_connect(&f.network, 1, WS_STA_IP));
    CHECK(concurrency_net_command(&f, 1, "C\r", "AZ=090\r\n"));
    return concurrency_close(&f);
}

static bool full_core_queue_stop(void)
{
    gs232b_motion_sim_t sim;
    gs232b_motion_config_t config;
    gs232b_fake_store_t store;
    gs232b_fake_sessions_t io;
    gs232b_dispatcher_t dispatcher;
    uint8_t requests[GS232B_LINE_STORAGE_BYTES];
    uint8_t lines[GS232B_CONTROL_MAX_SESSIONS][GS232B_LINE_STORAGE_BYTES];
    gs232b_session_key_t sessions[GS232B_CONTROL_MAX_SESSIONS];
    gs232b_motion_sim_config(&config);
    CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
    gs232b_fake_store_init(&store);
    CHECK(gs232b_fake_sessions_init(&io, sim.manager.clock) == GS232B_OK);
    CHECK(gs232b_dispatcher_init(&dispatcher, &sim.motion, gs232b_fake_store_port(&store),
        gs232b_fake_sessions_port(&io), requests, sizeof(requests)) == GS232B_OK);
    for (size_t i = 0; i < GS232B_CONTROL_MAX_SESSIONS; ++i) {
        gs232b_session_key_t key;
        CHECK(gs232b_fake_sessions_open(&io, GS232B_TRANSPORT_ETHERNET_WS, &key) == GS232B_OK);
        CHECK(gs232b_dispatcher_session_open(&dispatcher, GS232B_TRANSPORT_ETHERNET_WS,
            lines[i], sizeof(lines[i]), &sessions[i]) == GS232B_OK);
        CHECK(gs232b_session_key_equal(key, sessions[i]));
        gs232b_command_context_t context;
        CHECK(gs232b_dispatcher_capture(&dispatcher, key, &context) == GS232B_OK);
        for (size_t n = 0; n < GS232B_CONTROL_REQUESTS_PER_SESSION; ++n)
            CHECK(gs232b_dispatcher_feed(&dispatcher, context, (const uint8_t *)"C\r", 2) == GS232B_OK);
        CHECK(gs232b_dispatcher_feed(&dispatcher, context, (const uint8_t *)"C\r", 2) ==
            GS232B_ERROR_RESOURCE_EXHAUSTED);
    }
    CHECK(dispatcher.control.request_count == GS232B_CONTROL_MAX_REQUESTS);
    gs232b_command_context_t context;
    CHECK(gs232b_dispatcher_capture(&dispatcher, sessions[0], &context) == GS232B_OK);
    CHECK(gs232b_dispatcher_feed(&dispatcher, context, (const uint8_t *)"S\r", 2) == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&dispatcher) == GS232B_OK);
    CHECK(dispatcher.control.request_count == 0 && dispatcher.control.request_bytes_used == 0);
    return stopped(&sim);
}

bool concurrency_pressure_tests(void)
{
    return full_live_queue_stop() && query_flood_and_connection_exhaustion() && full_core_queue_stop();
}
