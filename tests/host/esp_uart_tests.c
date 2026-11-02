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

#include "gs232b/esp_uart.h"
#include "esp_uart_internal.h"
#include "uart_api.h"
#include "gs232b/fakes.h"
#include "motion_test_support.h"
#include <string.h>

typedef struct {
    gs232b_esp_uart_t uart;
    gs232b_motion_sim_t sim;
    gs232b_fake_store_t store;
    gs232b_dispatcher_t dispatcher;
    uint8_t requests[8192];
    uint8_t line[GS232B_LINE_STORAGE_BYTES];
} uart_fixture_t;

static bool drain(uart_fixture_t *f)
{
    for (unsigned i = 0; i < 128 && f->uart.tx_count != 0; ++i) {
        CHECK(gs232b_esp_uart_service(&f->uart) == GS232B_OK);
    }
    CHECK(f->uart.tx_count == 0);
    return true;
}

static bool wire(const char *expected)
{
    CHECK(fake_uart.wire_size == strlen(expected));
    CHECK(memcmp(fake_uart.wire, expected, fake_uart.wire_size) == 0);
    fake_uart.wire_size = 0;
    return true;
}

static bool boot(uart_fixture_t *f)
{
    fake_uart_reset();
    memset(f, 0, sizeof(*f));
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    CHECK(gs232b_motion_sim_init(&f->sim, &config, 0, WAIT_US) == GS232B_OK);
    gs232b_fake_store_init(&f->store);
    CHECK(gs232b_esp_uart_open(&f->uart, gs232b_fake_store_port(&f->store)) == GS232B_OK);
    fake_uart.now_us += (int64_t)GS232B_UART_BAUD_WINDOW_US;
    CHECK(gs232b_esp_uart_boot_service(&f->uart) == GS232B_OK);
    CHECK(gs232b_dispatcher_init(&f->dispatcher, &f->sim.motion,
        gs232b_fake_store_port(&f->store), gs232b_esp_uart_io(&f->uart),
        f->requests, sizeof(f->requests)) == GS232B_OK);
    CHECK(gs232b_esp_uart_attach(&f->uart, &f->dispatcher, f->line, sizeof(f->line)) == GS232B_OK);
    CHECK(drain(f));
    return wire("Connect OK\r\n");
}

static gs232b_error_t input(uart_fixture_t *f, const char *bytes)
{
    fake_uart_input(bytes, strlen(bytes));
    gs232b_uart_rx_step(&f->uart);
    return gs232b_esp_uart_service(&f->uart);
}

static bool command(uart_fixture_t *f, const char *bytes, const char *response)
{
    CHECK(input(f, bytes) == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f->dispatcher) == GS232B_OK);
    CHECK(gs232b_esp_uart_service(&f->uart) == GS232B_OK);
    CHECK(drain(f));
    return wire(response);
}

static bool close_fixture(uart_fixture_t *f)
{
    CHECK(gs232b_esp_uart_close(&f->uart) == GS232B_OK);
    CHECK(gs232b_esp_uart_close(&f->uart) == GS232B_OK);
    CHECK(!fake_uart.installed && fake_uart.live_queues == 0 &&
        fake_uart.live_semaphores == 0 && fake_uart.live_tasks == 0);
    CHECK(gs232b_esp_uart_service(&f->uart) == GS232B_ERROR_NOT_READY);
    return stopped(&f->sim);
}

static bool test_board_and_byte_isolation(void)
{
    uart_fixture_t f;
    CHECK(boot(&f));
    CHECK(fake_uart.config.baud_rate == 9600);
    CHECK(fake_uart.config.data_bits == UART_DATA_8_BITS);
    CHECK(fake_uart.config.parity == UART_PARITY_DISABLE);
    CHECK(fake_uart.config.stop_bits == UART_STOP_BITS_1);
    CHECK(fake_uart.config.flow_ctrl == UART_HW_FLOWCTRL_DISABLE);
    CHECK(fake_uart.tx_pin == 6 && fake_uart.rx_pin == 7);
    CHECK(fake_uart.rts_pin == UART_PIN_NO_CHANGE && fake_uart.cts_pin == UART_PIN_NO_CHANGE);
    CHECK(fake_uart.rx_size == 32768 && fake_uart.tx_size == 0);
    CHECK(fake_uart.task_entry == gs232b_uart_rx_task && fake_uart.task_ctx == &f.uart);
    CHECK(fake_uart.task_priority == 8 && fake_uart.task_stack >= 3072);
    CHECK(gs232b_esp_uart_io(&f.uart).receive == NULL);
    /* NOTE: 标准日志输出不能落入协议 TX；同样内容也不能凭空成为控制输入。 */
    fputs("USB console log: R\r\n", stderr);
    CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(wire(""));
    CHECK(input(&f, "c") == GS232B_OK);
    CHECK(f.dispatcher.control.request_count == 0);
    CHECK(command(&f, "\r\n", "AZ=090\r\n"));

    const uint8_t bytes[] = {0, '\n', '\r', 0xff, 'x'};
    gs232b_session_io_t io = gs232b_esp_uart_io(&f.uart);
    CHECK(gs232b_session_send(&io, f.uart.session, bytes, sizeof(bytes)) == GS232B_OK);
    CHECK(drain(&f));
    CHECK(fake_uart.wire_size == sizeof(bytes) && memcmp(bytes, fake_uart.wire, sizeof(bytes)) == 0);
    fake_uart.wire_size = 0;
    CHECK(close_fixture(&f));
    return true;
}

static bool test_tx_bounds_partial_and_generation(void)
{
    uart_fixture_t f;
    CHECK(boot(&f));
    uint8_t bytes[GS232B_UART_TX_BYTES];
    for (size_t i = 0; i < sizeof(bytes); ++i) { bytes[i] = (uint8_t)i; }
    gs232b_session_io_t io = gs232b_esp_uart_io(&f.uart);
    gs232b_session_key_t old = f.uart.session;
    gs232b_session_key_t wrong = old; ++wrong.generation;
    CHECK(gs232b_session_send(&io, wrong, bytes, 1) == GS232B_ERROR_STALE_SESSION);
    CHECK(gs232b_session_send(&io, old, bytes, sizeof(bytes)) == GS232B_OK);
    CHECK(gs232b_session_send(&io, old, bytes, 1) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(f.uart.tx_count == sizeof(bytes));
    fake_uart.tx_limit = 0;
    CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
    CHECK(f.uart.tx_count == sizeof(bytes));
    fake_uart.tx_limit = 37;
    CHECK(drain(&f));
    CHECK(fake_uart.wire_size == sizeof(bytes));
    CHECK(memcmp(fake_uart.wire, bytes, sizeof(bytes)) == 0);
    fake_uart.wire_size = 0;
    CHECK(gs232b_session_send(&io, old, bytes, 200) == GS232B_OK);
    CHECK(drain(&f));
    CHECK(fake_uart.wire_size == 200 && memcmp(fake_uart.wire, bytes, 200) == 0);
    fake_uart.wire_size = 0;
    CHECK(gs232b_dispatcher_session_close(&f.dispatcher, old) == GS232B_OK);
    CHECK(gs232b_session_send(&io, old, bytes, 1) == GS232B_ERROR_STALE_SESSION);
    fake_uart.tx_busy = true;
    CHECK(gs232b_esp_uart_attach(&f.uart, &f.dispatcher, f.line, sizeof(f.line)) == GS232B_ERROR_WOULD_BLOCK);
    fake_uart.tx_busy = false;
    CHECK(gs232b_esp_uart_attach(&f.uart, &f.dispatcher, f.line, sizeof(f.line)) == GS232B_OK);
    CHECK(!gs232b_session_key_equal(old, f.uart.session));
    CHECK(gs232b_session_send(&io, old, bytes, 1) == GS232B_ERROR_STALE_SESSION);
    CHECK(drain(&f));
    CHECK(wire("Connect OK\r\n"));
    return close_fixture(&f);
}

static bool test_rx_backpressure_and_maximum_line(void)
{
    uart_fixture_t f;
    CHECK(boot(&f));
    uint8_t text[16385];
    memset(text, ' ', sizeof(text));
    text[0] = 'M';
    memcpy(text + 1, "001 100 120", 11);
    text[sizeof(text) - 1] = '\r';
    fake_uart_input(text, sizeof(text));
    for (unsigned i = 0; i < GS232B_UART_RX_PACKETS + 2; ++i) {
        gs232b_uart_rx_step(&f.uart);
    }
    CHECK(uxQueueMessagesWaiting(f.uart.packets) == GS232B_UART_RX_PACKETS);
    CHECK(fake_uart.rx_count == sizeof(text) - GS232B_UART_RX_PACKETS * 256);
    CHECK(!f.uart.loss_pending);
    for (unsigned i = 0; i < 100 && (fake_uart.rx_count != 0 ||
            uxQueueMessagesWaiting(f.uart.packets) != 0); ++i) {
        CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
        gs232b_uart_rx_step(&f.uart);
    }
    CHECK(fake_uart.rx_count == 0 && uxQueueMessagesWaiting(f.uart.packets) == 0);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(drain(&f));
    CHECK(wire("\r"));
    CHECK(f.dispatcher.control.sequence_state.state == GS232B_SEQUENCE_LOADED_WAIT_T);
    return close_fixture(&f);
}

static bool test_loss_stops_and_resynchronizes(void)
{
    static const uart_event_type_t losses[] = {
        UART_FIFO_OVF, UART_BUFFER_FULL, UART_FRAME_ERR, UART_PARITY_ERR, UART_BREAK
    };
    for (size_t i = 0; i < sizeof(losses) / sizeof(losses[0]); ++i) {
        uart_fixture_t f;
        CHECK(boot(&f));
        CHECK(command(&f, "W010 180 120 200 130\r", "\r"));
        gs232b_session_key_t owner = f.uart.session;
        CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, owner));
        CHECK(input(&f, "R") == GS232B_OK);
        fake_uart_input("\rL\r", 3);
        gs232b_uart_rx_step(&f.uart);
        CHECK(uxQueueMessagesWaiting(f.uart.packets) == 1);
        fake_uart_event(losses[i]);
        fake_uart_event(UART_FRAME_ERR);
        gs232b_uart_rx_step(&f.uart);
        CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, owner));
        CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
        CHECK(stopped(&f.sim));
        CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
        CHECK(f.dispatcher.control.sequence_state.state == GS232B_SEQUENCE_EMPTY);
        CHECK(f.dispatcher.control.request_count == 0);
        CHECK(drain(&f));
        CHECK(wire("?>\r\n"));
        /* NOTE: 同步故障推进后的 epoch，丢失前缀后的 R 不得执行；下个完整查询恢复。 */
        CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
        CHECK(command(&f, "R\rC\r", "AZ=090\r\n"));
        CHECK(stopped(&f.sim));
        CHECK(command(&f, "R\r", "\r"));
        CHECK(close_fixture(&f));
    }
    return true;
}

static bool test_event_saturation_read_and_tx_failure(void)
{
    for (int failure = 0; failure < 4; ++failure) {
        uart_fixture_t f;
        CHECK(boot(&f));
        CHECK(command(&f, "R\r", "\r"));
        if (failure == 0) {
            for (unsigned i = 0; i < GS232B_UART_EVENTS + 5; ++i) { fake_uart_event(UART_DATA); }
        } else if (failure == 1) {
            fake_uart.fail_read = true;
        } else if (failure == 2) {
            uint8_t full[32768] = {0};
            fake_uart_input(full, sizeof(full));
            fake_uart_input("S\r", 2);
        } else {
            fake_uart.fail_tx = true;
            gs232b_session_io_t io = gs232b_esp_uart_io(&f.uart);
            CHECK(gs232b_session_send(&io, f.uart.session, (const uint8_t *)"x", 1) == GS232B_OK);
            CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_ERROR_IO);
            CHECK(!gs232b_session_key_valid(f.uart.session));
            CHECK(stopped(&f.sim));
            CHECK(close_fixture(&f));
            continue;
        }
        gs232b_uart_rx_step(&f.uart);
        CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
        CHECK(stopped(&f.sim));
        CHECK(drain(&f));
        CHECK(wire("?>\r\n"));
        CHECK(close_fixture(&f));
    }
    return true;
}

static bool test_epoch_barrier_and_idle_is_not_disconnect(void)
{
    uart_fixture_t f;
    CHECK(boot(&f));
    CHECK(command(&f, "R\r", "\r"));
    for (unsigned i = 0; i < 500; ++i) {
        gs232b_uart_rx_step(&f.uart);
        CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
    }
    CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, f.uart.session));
    CHECK(command(&f, "S\r", "\r"));
    fake_uart_input("R\r", 2);
    gs232b_uart_rx_step(&f.uart);
    gs232b_command_context_t context;
    CHECK(gs232b_dispatcher_capture(&f.dispatcher, f.uart.session, &context) == GS232B_OK);
    CHECK(gs232b_dispatcher_feed(&f.dispatcher, context, (const uint8_t *)"S\r", 2) == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
    CHECK(f.dispatcher.control.request_count == 0);
    CHECK(f.dispatcher.control.sessions[f.uart.session.session_id - 1].parser.discarding);
    CHECK(command(&f, "R\rC\r", "\rAZ=090\r\n"));
    CHECK(stopped(&f.sim));
    CHECK(xSemaphoreTake(f.uart.rx_lock, 0) == pdPASS);
    CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(xSemaphoreGive(f.uart.rx_lock) == pdPASS);
    return close_fixture(&f);
}

static bool test_lifecycle_failure_cleanup(void)
{
    gs232b_fake_store_t fake_store;
    gs232b_fake_store_init(&fake_store);
    const gs232b_store_t store = gs232b_fake_store_port(&fake_store);
    for (int stage = 1; stage <= 5; ++stage) {
        fake_uart_reset();
        gs232b_esp_uart_t uart = {0};
        fake_uart.fail_alloc_at = stage;
        CHECK(gs232b_esp_uart_open(&uart, store) == GS232B_ERROR_RESOURCE_EXHAUSTED);
        CHECK(gs232b_esp_uart_close(&uart) == GS232B_OK);
        CHECK(fake_uart.live_queues == 0 && fake_uart.live_semaphores == 0);
    }
    for (int stage = 1; stage <= 3; ++stage) {
        fake_uart_reset();
        gs232b_esp_uart_t uart = {0};
        fake_uart.fail_init_at = stage;
        CHECK(gs232b_esp_uart_open(&uart, store) == GS232B_ERROR_IO);
        CHECK(gs232b_esp_uart_close(&uart) == GS232B_OK);
    }
    fake_uart_reset();
    gs232b_esp_uart_t uart = {0};
    CHECK(gs232b_esp_uart_open(&uart, (gs232b_store_t){0}) == GS232B_ERROR_INVALID_ARGUMENT);
    fake_uart.fail_task = true;
    CHECK(gs232b_esp_uart_open(&uart, store) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    fake_uart.fail_task = false;
    uint64_t revision;
    CHECK(gs232b_uart_baud_commit(&store, 0, 1200, &revision) == GS232B_OK);
    CHECK(gs232b_esp_uart_open(&uart, store) == GS232B_OK);
    CHECK(fake_uart.config.baud_rate == 1200);
    gs232b_esp_uart_t other = {0};
    CHECK(gs232b_esp_uart_open(&other, store) == GS232B_ERROR_BUSY);
    fake_uart.defer_exit = true;
    CHECK(gs232b_esp_uart_close(&uart) == GS232B_ERROR_TIMEOUT);
    CHECK(fake_uart.installed && uart.rx_task != NULL);
    fake_uart.defer_exit = false;
    fake_uart.fail_delete = true;
    CHECK(gs232b_esp_uart_close(&uart) == GS232B_ERROR_IO);
    CHECK(fake_uart.installed && uart.rx_task == NULL);
    fake_uart.fail_delete = false;
    CHECK(gs232b_esp_uart_close(&uart) == GS232B_OK);
    CHECK(fake_uart.live_queues == 0 && fake_uart.live_semaphores == 0 && fake_uart.live_tasks == 0);
    return true;
}

static bool test_persistent_loss_and_tx_congestion_fail_closed(void)
{
    uart_fixture_t f;
    CHECK(boot(&f));
    CHECK(command(&f, "R\r", "\r"));
    fake_uart.fail_flush = true;
    fake_uart_event(UART_FIFO_OVF);
    gs232b_uart_rx_step(&f.uart);
    for (unsigned i = 0; i < 10; ++i) {
        CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_ERROR_IO);
        CHECK(stopped(&f.sim));
    }
    CHECK(wire("?>\r\n"));
    CHECK(f.uart.loss_pending);
    fake_uart.fail_flush = false;
    CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
    CHECK(!f.uart.loss_pending);
    CHECK(command(&f, "R\rC\r", "AZ=090\r\n"));
    CHECK(command(&f, "R\r", "\r"));
    gs232b_session_io_t io = gs232b_esp_uart_io(&f.uart);
    uint8_t full[GS232B_UART_TX_BYTES] = {0};
    CHECK(gs232b_session_send(&io, f.uart.session, full, sizeof(full)) == GS232B_OK);
    fake_uart.tx_limit = 0;
    CHECK(input(&f, "C\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(!gs232b_session_key_valid(f.uart.session));
    CHECK(f.uart.tx_count == 0);
    CHECK(stopped(&f.sim));
    return close_fixture(&f);
}

int main(void)
{
    return test_board_and_byte_isolation() && test_tx_bounds_partial_and_generation() &&
        test_rx_backpressure_and_maximum_line() && test_loss_stops_and_resynchronizes() &&
        test_event_saturation_read_and_tx_failure() && test_epoch_barrier_and_idle_is_not_disconnect() &&
        test_lifecycle_failure_cleanup() && test_persistent_loss_and_tx_congestion_fail_closed()
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
