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

#undef send

static gs232b_session_io_t route(concurrency_fixture_t *f, gs232b_session_key_t session)
{
    return f->uart.opening || (gs232b_session_key_valid(f->uart.session) &&
        gs232b_session_key_equal(f->uart.session, session)) ?
        gs232b_esp_uart_io(&f->uart) : gs232b_esp_ws_io(&f->network.ws);
}

static gs232b_error_t send_session(void *ctx, gs232b_session_key_t session,
    const uint8_t *bytes, size_t size)
{
    const gs232b_session_io_t io = route(ctx, session);
    return gs232b_session_send(&io, session, bytes, size);
}

static gs232b_error_t close_session(void *ctx, gs232b_session_key_t session)
{
    const gs232b_session_io_t io = route(ctx, session);
    return gs232b_session_close(&io, session);
}

static bool handshake_wait(void)
{
    if (fake_ws.control_step == NULL) return false;
    fake_ws.control_step(fake_ws.control_ctx);
    return true;
}

void concurrency_time(concurrency_fixture_t *f, gs232b_time_us_t now)
{
    f->network.sim.now_us = now;
    fake_uart.now_us = (int64_t)now;
    fake_ws.now_us = now;
}

bool concurrency_bounds(concurrency_fixture_t *f)
{
    const gs232b_dispatcher_t *d = &f->network.dispatcher;
    CHECK(d->control.request_count <= GS232B_CONTROL_MAX_REQUESTS);
    CHECK(d->control.request_bytes_used <= GS232B_LINE_STORAGE_BYTES);
    CHECK(f->uart.tx_count <= GS232B_UART_TX_BYTES);
    CHECK(uxQueueMessagesWaiting(f->uart.packets) <= GS232B_UART_RX_PACKETS);
    CHECK(uxQueueMessagesWaiting(f->uart.events) <= GS232B_UART_EVENTS);
    CHECK(fake_uart.rx_count <= GS232B_UART_RX_RING_BYTES);
    if (f->spi_open) CHECK(atomic_load(&f->spi.safe));
    for (size_t i = 0; i < GS232B_CONTROL_MAX_SESSIONS; ++i) {
        CHECK(d->control.sessions[i].pending_requests <= GS232B_CONTROL_REQUESTS_PER_SESSION);
        CHECK(d->sessions[i].count <= GS232B_DISPATCHER_REPLIES_PER_SESSION);
        CHECK(d->control.sessions[i].parser.stored <= GS232B_LINE_STORAGE_BYTES);
        CHECK(d->control.sessions[i].parser.position.fields <= GS232B_POSITION_MAX_ANGLES + 1U);
    }
    for (size_t i = 0; i < GS232B_WS_CLIENTS; ++i) {
        const gs232b_ws_client_t *c = &f->network.ws.clients[i];
        CHECK(c->rx_count <= GS232B_WS_RX_PACKETS && c->tx_count <= GS232B_WS_TX_BYTES);
        CHECK(c->control_count <= GS232B_WS_CONTROL_PACKETS);
        CHECK(c->wire_sent <= c->wire_size && c->wire_size <= sizeof(c->wire));
    }
    CHECK(!f->network.sim.unsafe_output);
    return true;
}

bool concurrency_uart_expect(const char *expected)
{
    CHECK(fake_uart.wire_size == strlen(expected));
    CHECK(memcmp(fake_uart.wire, expected, fake_uart.wire_size) == 0);
    fake_uart.wire_size = 0;
    return true;
}

bool concurrency_net_expect(int fd, const char *expected)
{
    uint8_t bytes[4096], frame[GS232B_WS_CHUNK_BYTES];
    size_t used = 0, size;
    httpd_ws_type_t type;
    while (fake_ws_take_frame(fd, &type, frame, sizeof(frame), &size)) {
        if (type == HTTPD_WS_TYPE_PING) {
            CHECK(fake_ws_frame(fd, HTTPD_WS_TYPE_PONG, true, frame, size) == ESP_OK);
        } else {
            CHECK(type == HTTPD_WS_TYPE_BINARY && size <= sizeof(bytes) - used);
            memcpy(bytes + used, frame, size);
            used += size;
        }
    }
    CHECK(used == strlen(expected) && memcmp(bytes, expected, used) == 0);
    CHECK(fake_ws.sockets[fd].output_size == 0);
    return true;
}

bool concurrency_poll(concurrency_fixture_t *f, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (!gs232b_session_key_valid(f->uart.session)) {
            CHECK(gs232b_esp_uart_attach(&f->uart, &f->network.dispatcher,
                f->line, sizeof(f->line)) == GS232B_OK);
            CHECK(gs232b_esp_uart_service(&f->uart) == GS232B_OK);
            CHECK(concurrency_uart_expect("Connect OK\r\n"));
        }
        gs232b_uart_rx_step(&f->uart);
        CHECK(gs232b_esp_uart_service(&f->uart) == GS232B_OK);
        const gs232b_error_t ws_result = gs232b_esp_ws_service(&f->network.ws);
        if (ws_result != GS232B_OK) fprintf(stderr, " ws service=%s time=%llu\n",
            gs232b_error_name(ws_result), (unsigned long long)f->network.sim.now_us);
        CHECK(ws_result == GS232B_OK);
        CHECK(gs232b_dispatcher_tick(&f->network.dispatcher) == GS232B_OK);
        CHECK(gs232b_esp_uart_service(&f->uart) == GS232B_OK);
        fake_ws_run_work();
        CHECK(concurrency_bounds(f));
    }
    return true;
}

bool concurrency_uart_input(concurrency_fixture_t *f, const char *input)
{
    fake_uart_input(input, strlen(input));
    gs232b_uart_rx_step(&f->uart);
    CHECK(gs232b_esp_uart_service(&f->uart) == GS232B_OK);
    return concurrency_bounds(f);
}

bool concurrency_enable_spi(concurrency_fixture_t *f)
{
    CHECK(gs232b_fake_spi_bus_init(&f->spi, &f->bus));
    f->spi_open = true;
    CHECK(gs232b_shared_spi_set_ethernet_enabled(&f->bus, true) == GS232B_OK);
    f->network.sim.manager.driver = gs232b_shared_spi_output_driver(&f->bus);
    CHECK(gs232b_output_manager_stop_all(&f->network.sim.manager) == GS232B_OK);
    gs232b_fake_spi_bus_clear(&f->spi);
    return true;
}

bool concurrency_net_command(concurrency_fixture_t *f, int fd, const char *input,
    const char *expected)
{
    CHECK(fake_ws_frame(fd, HTTPD_WS_TYPE_BINARY, true, input, strlen(input)) == ESP_OK);
    CHECK(concurrency_poll(f, 12));
    return concurrency_net_expect(fd, expected);
}

bool concurrency_init(concurrency_fixture_t *f)
{
    fake_uart_reset();
    fake_ws_reset();
    memset(f, 0, sizeof(*f));
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    CHECK(gs232b_motion_sim_init(&f->network.sim, &config, 0, WAIT_US) == GS232B_OK);
    gs232b_fake_store_init(&f->network.store);
    const gs232b_store_t store = gs232b_fake_store_port(&f->network.store);
    CHECK(gs232b_esp_uart_open(&f->uart, store) == GS232B_OK);
    concurrency_time(f, GS232B_UART_BAUD_WINDOW_US);
    CHECK(gs232b_esp_uart_boot_service(&f->uart) == GS232B_OK);
    const gs232b_session_io_t io = {.ctx = f, .send = send_session, .close = close_session};
    CHECK(gs232b_dispatcher_init(&f->network.dispatcher, &f->network.sim.motion, store, io,
        f->network.requests, GS232B_LINE_STORAGE_BYTES) == GS232B_OK);
    CHECK(gs232b_esp_uart_attach(&f->uart, &f->network.dispatcher, f->line, sizeof(f->line)) == GS232B_OK);
    CHECK(gs232b_esp_uart_service(&f->uart) == GS232B_OK);
    CHECK(concurrency_uart_expect("Connect OK\r\n"));
    CHECK(gs232b_esp_ws_open(&f->network.ws, &f->network.dispatcher) == GS232B_OK);
    CHECK(gs232b_esp_ws_interface(&f->network.ws, GS232B_TRANSPORT_ETHERNET_WS,
        WS_ETH_IP, 1, WS_ETH_IF_NAME) == GS232B_OK);
    CHECK(gs232b_esp_ws_interface(&f->network.ws, GS232B_TRANSPORT_WIFI_WS,
        WS_STA_IP, 1, WS_STA_IF_NAME) == GS232B_OK);
    fake_ws.control_step = ws_control_step;
    fake_ws.control_ctx = &f->network.ws;
    fake_uart.binary_wait = handshake_wait;
    for (int fd = 0; fd < (int)GS232B_WS_CLIENTS; ++fd) {
        CHECK(ws_connect(&f->network, fd, fd % 2 == 0 ? WS_ETH_IP : WS_STA_IP));
    }
    return concurrency_bounds(f);
}

bool concurrency_close(concurrency_fixture_t *f)
{
    CHECK(gs232b_esp_ws_close(&f->network.ws) == GS232B_OK);
    fake_ws.control_step = NULL;
    fake_uart.binary_wait = NULL;
    CHECK(gs232b_esp_uart_close(&f->uart) == GS232B_OK);
    CHECK(fake_uart.live_queues == 0 && fake_uart.live_semaphores == 0 &&
        fake_uart.live_tasks == 0 && !fake_uart.installed && !fake_ws.started);
    if (f->spi_open) gs232b_fake_spi_bus_destroy(&f->spi);
    return stopped(&f->network.sim);
}
