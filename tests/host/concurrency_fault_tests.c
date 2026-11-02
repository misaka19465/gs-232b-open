#include "concurrency_test_support.h"

typedef struct { concurrency_fixture_t *fixture; unsigned remaining; } spi_fault_t;

static void fail_shifts(void *ctx, spi_event_kind_t kind)
{
    spi_fault_t *fault = ctx;
    concurrency_fixture_t *f = fault->fixture;
    if (kind == SPI_SHIFT_BYTE) {
        gs232b_fake_spi_bus_fault(&f->spi, SPI_SHIFT_BYTE, 0, GS232B_ERROR_IO);
        if (--fault->remaining == 0) f->spi.hook = NULL;
    }
}

static bool stale_epoch_and_watchdog(void)
{
    concurrency_fixture_t f;
    gs232b_command_context_t old;
    CHECK(concurrency_init(&f));
    CHECK(gs232b_dispatcher_capture(&f.network.dispatcher, ws_client(&f.network, 0)->session,
        &old) == GS232B_OK);
    CHECK(concurrency_uart_input(&f, "R\r"));
    CHECK(concurrency_poll(&f, 2));
    CHECK(concurrency_uart_expect("\r"));
    CHECK(concurrency_net_command(&f, 1, "S\r", "\r"));
    CHECK(gs232b_dispatcher_feed(&f.network.dispatcher, old, (const uint8_t *)"R\r", 2) ==
        GS232B_ERROR_CONFLICT);
    CHECK(gs232b_dispatcher_tick(&f.network.dispatcher) == GS232B_OK);
    CHECK(stopped(&f.network.sim));
    CHECK(concurrency_uart_input(&f, "R\r"));
    CHECK(concurrency_poll(&f, 2));
    CHECK(concurrency_uart_expect("\r"));
    const gs232b_session_key_t owner = f.uart.session;
    CHECK(gs232b_dispatcher_watchdog_set(&f.network.dispatcher, owner, 1000) == GS232B_OK);
    concurrency_time(&f, f.network.sim.now_us + UINT64_C(1000000));
    CHECK(gs232b_dispatcher_tick(&f.network.dispatcher) == GS232B_ERROR_TIMEOUT);
    CHECK(!gs232b_session_key_valid(f.network.dispatcher.control.owner));
    CHECK(!gs232b_session_key_valid(f.uart.session));
    CHECK(stopped(&f.network.sim));
    CHECK(concurrency_poll(&f, 1));
    CHECK(f.uart.session.generation > owner.generation);
    CHECK(concurrency_net_command(&f, 3, "C\r", "AZ=090\r\n"));
    return concurrency_close(&f);
}

static bool queued_old_epochs(void)
{
    concurrency_fixture_t f;
    CHECK(concurrency_init(&f));
    CHECK(concurrency_net_command(&f, 0, "R\r", "\r"));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, true, "L\r", 2) == ESP_OK);
    fake_uart_input("U\r", 2);
    gs232b_uart_rx_step(&f.uart);
    CHECK(uxQueueMessagesWaiting(f.uart.packets) == 1);
    const gs232b_command_epoch_t old_epoch = f.network.dispatcher.control.epoch;
    CHECK(fake_ws_frame(1, HTTPD_WS_TYPE_BINARY, true, "S\r", 2) == ESP_OK);
    /* NOTE: 两条动作都已盖旧章；先消费另一连接的停止，再消费旧包。 */
    f.network.ws.cursor = 1;
    CHECK(gs232b_esp_ws_service(&f.network.ws) == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.network.dispatcher) == GS232B_OK);
    CHECK(f.network.dispatcher.control.epoch > old_epoch);
    CHECK(stopped(&f.network.sim));
    CHECK(gs232b_esp_ws_service(&f.network.ws) == GS232B_ERROR_CONFLICT);
    CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
    CHECK(uxQueueMessagesWaiting(f.uart.packets) == 0);
    CHECK(concurrency_poll(&f, 2));
    CHECK(!gs232b_session_key_valid(f.network.dispatcher.control.owner));
    CHECK(concurrency_net_expect(0, ""));
    CHECK(concurrency_net_expect(1, "\r"));
    CHECK(concurrency_uart_expect(""));
    CHECK(concurrency_uart_input(&f, "\r"));
    CHECK(concurrency_poll(&f, 2));
    CHECK(concurrency_uart_expect(""));
    CHECK(concurrency_uart_input(&f, "C\r"));
    CHECK(concurrency_poll(&f, 2));
    CHECK(concurrency_uart_expect("AZ=090\r\n"));
    CHECK(concurrency_net_command(&f, 0, "C\r", "AZ=090\r\n"));
    CHECK(stopped(&f.network.sim));
    return concurrency_close(&f);
}

static bool spi_failure_and_retry(bool persistent)
{
    concurrency_fixture_t f;
    CHECK(concurrency_init(&f));
    CHECK(concurrency_enable_spi(&f));
    CHECK(concurrency_uart_input(&f, "R\r"));
    CHECK(concurrency_poll(&f, 2));
    CHECK(concurrency_uart_expect("\r"));
    spi_fault_t fault = {.fixture = &f, .remaining = persistent ? 2U : 1U};
    CHECK((f.spi.latched & 0x0fU) != 0 && f.spi.pwm > 0);
    f.spi.hook = fail_shifts;
    f.spi.hook_ctx = &fault;
    CHECK(concurrency_uart_input(&f, "S\r"));
    CHECK(gs232b_dispatcher_tick(&f.network.dispatcher) == GS232B_ERROR_IO);
    CHECK(f.network.sim.motion.state.fault.code == GS232B_FAULT_OUTPUT);
    if (persistent) CHECK(!f.network.sim.motion.state.output.known);
    else CHECK(stopped(&f.network.sim));
    CHECK(!gs232b_session_key_valid(f.network.dispatcher.control.owner));
    CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
    CHECK(concurrency_uart_expect("?>\r\n"));
    const size_t attempts = f.spi.count;
    CHECK(gs232b_dispatcher_tick(&f.network.dispatcher) == GS232B_ERROR_IO);
    CHECK(f.spi.count == attempts);
    CHECK(gs232b_motion_clear_fault(&f.network.sim.motion) == GS232B_OK);
    CHECK((f.spi.latched & 0x0fU) == 0 && f.spi.pwm == 0);
    CHECK(concurrency_poll(&f, 1));
    CHECK(concurrency_net_command(&f, 2, "C\r", "AZ=090\r\n"));
    return concurrency_close(&f);
}

static bool slow_owner_and_nvs_failure(void)
{
    concurrency_fixture_t f;
    uint8_t fill[GS232B_WS_TX_BYTES];
    memset(fill, 0xa5, sizeof(fill));
    CHECK(concurrency_init(&f));
    CHECK(concurrency_net_command(&f, 0, "R\r", "\r"));
    fake_ws.sockets[0].send_limit = 0;
    const gs232b_session_key_t owner = ws_client(&f.network, 0)->session;
    const gs232b_session_io_t io = gs232b_esp_ws_io(&f.network.ws);
    CHECK(gs232b_session_send(&io, owner, fill, sizeof(fill)) == GS232B_OK);
    CHECK(gs232b_esp_ws_service(&f.network.ws) == GS232B_OK);
    concurrency_time(&f, f.network.sim.now_us + GS232B_WS_SEND_TIMEOUT_US);
    CHECK(gs232b_esp_ws_service(&f.network.ws) == GS232B_OK);
    CHECK(stopped(&f.network.sim));
    CHECK(!gs232b_session_key_valid(f.network.dispatcher.control.owner));
    CHECK(concurrency_poll(&f, 1));
    CHECK(!fake_ws.sockets[0].active);
    CHECK(concurrency_uart_input(&f, "C\r"));
    CHECK(concurrency_poll(&f, 2));
    CHECK(concurrency_uart_expect("AZ=090\r\n"));
    CHECK(fake_ws.sockets[1].active && fake_ws.sockets[2].active && fake_ws.sockets[3].active);

    CHECK(gs232b_fake_fail_next(&f.network.store.failures[GS232B_STORE_DISPLAY]
        [GS232B_FAKE_STORE_INTERRUPT], GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(fake_ws_frame(1, HTTPD_WS_TYPE_BINARY, true, "P36\r", 4) == ESP_OK);
    CHECK(gs232b_esp_ws_service(&f.network.ws) == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.network.dispatcher) == GS232B_ERROR_IO);
    CHECK(concurrency_poll(&f, 2));
    CHECK(concurrency_net_expect(1, "?>\r\n"));
    CHECK(f.network.dispatcher.display.mode == GS232B_AZIMUTH_DISPLAY_P45);
    CHECK(concurrency_net_command(&f, 2, "P36\r", "\r"));
    CHECK(f.network.dispatcher.display.mode == GS232B_AZIMUTH_DISPLAY_P36);
    return concurrency_close(&f);
}

bool concurrency_fault_tests(void)
{
    return stale_epoch_and_watchdog() && queued_old_epochs() && spi_failure_and_retry(false) &&
        spi_failure_and_retry(true) && slow_owner_and_nvs_failure();
}
