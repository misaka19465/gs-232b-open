#include "ws_test_support.h"
#include <errno.h>

static bool bounded_tx_short_writes_and_stall(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(ws_connect(&f, 1, WS_STA_IP));
    fake_ws.sockets[0].send_limit = 1;
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, true, "C\r", 2) == ESP_OK);
    CHECK(ws_poll(&f, 32));
    CHECK(ws_expect_bytes(0, "AZ=090\r\n"));
    CHECK(ws_command(&f, 0, "R\r", "\r"));
    fake_ws.sockets[0].send_limit = 0;
    const gs232b_session_key_t owner = ws_client(&f, 0)->session;
    uint8_t fill[GS232B_WS_TX_BYTES];
    memset(fill, 0xa5, sizeof(fill));
    const gs232b_session_io_t io = gs232b_esp_ws_io(&f.ws);
    CHECK(gs232b_session_send(&io, owner, fill, sizeof(fill)) == GS232B_OK);
    const size_t count = ws_client(&f, 0)->tx_count;
    CHECK(gs232b_session_send(&io, owner, fill, 1) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(ws_client(&f, 0)->tx_count == count);
    CHECK(ws_poll(&f, 1));
    CHECK(ws_client(&f, 0)->wire_size == 260);
    CHECK(ws_command(&f, 1, "C\r", "AZ=090\r\n"));
    fake_ws.now_us += GS232B_WS_SEND_TIMEOUT_US;
    CHECK(ws_poll(&f, 2));
    CHECK(!fake_ws.sockets[0].active && fake_ws.sockets[1].active);
    CHECK(stopped(&f.sim));
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(gs232b_session_send(&io, owner, fill, 1) == GS232B_ERROR_STALE_SESSION);
    return ws_close(&f);
}

static bool disconnect_reuse_and_queued_input(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(ws_command(&f, 0, "W010 090 090 180 120\r", "\r"));
    const gs232b_session_key_t old = ws_client(&f, 0)->session;
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, false, "R\rM", 3) == ESP_OK);
    fake_ws_disconnect(0);
    CHECK(ws_poll(&f, 1));
    CHECK(stopped(&f.sim));
    CHECK(f.dispatcher.control.sequence_state.state == GS232B_SEQUENCE_EMPTY);
    CHECK(f.dispatcher.control.request_count == 0);
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    const gs232b_session_key_t current = ws_client(&f, 0)->session;
    CHECK(current.session_id == old.session_id && current.generation > old.generation);
    CHECK(ws_command(&f, 0, "C\r", "AZ=090\r\n"));
    gs232b_session_io_t io = gs232b_esp_ws_io(&f.ws);
    CHECK(gs232b_session_send(&io, old, (const uint8_t *)"R\r", 2) == GS232B_ERROR_STALE_SESSION);
    CHECK(gs232b_session_close(&io, old) == GS232B_ERROR_STALE_SESSION);
    CHECK(fake_ws.sockets[0].active);
    CHECK(ws_command(&f, 0, "R\r", "\r"));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_CLOSE, true, NULL, 0) == ESP_FAIL);
    CHECK(ws_poll(&f, 1));
    CHECK(stopped(&f.sim));
    return ws_close(&f);
}

static bool admission_interfaces_and_resource_cleanup(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(fake_ws_connect(0, 0xc0a80401) == ESP_FAIL);
    CHECK(ws_poll(&f, 1));
    for (int fd = 0; fd < 4; ++fd) { CHECK(ws_connect(&f, fd, fd % 2 == 0 ? WS_ETH_IP : WS_STA_IP)); }
    CHECK(fake_ws_connect(4, WS_ETH_IP) == ESP_FAIL);
    CHECK(ws_command(&f, 0, "R\r", "\r"));
    CHECK(gs232b_esp_ws_interface(&f.ws, GS232B_TRANSPORT_WIFI_WS, 0, 2, NULL) == GS232B_OK);
    CHECK(ws_poll(&f, 2));
    CHECK(fake_ws.sockets[0].active && fake_ws.sockets[2].active);
    CHECK(!fake_ws.sockets[1].active && !fake_ws.sockets[3].active);
    CHECK(gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(gs232b_esp_ws_interface(&f.ws, GS232B_TRANSPORT_ETHERNET_WS,
        WS_ETH_IP, 2, WS_ETH_IF_NAME) == GS232B_OK);
    CHECK(ws_poll(&f, 2));
    CHECK(stopped(&f.sim));
    CHECK(ws_close(&f));
    CHECK(ws_init(&f));
    fake_ws.fail_queue = true;
    CHECK(gs232b_esp_ws_service(&f.ws) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(!atomic_load(&f.ws.pump_pending));
    fake_ws.fail_queue = false;
    CHECK(ws_poll(&f, 1));
    CHECK(ws_close(&f));
    for (size_t allocation = 1; allocation <= 5; ++allocation) {
        CHECK(ws_init(&f));
        CHECK(ws_close(&f));
        fake_ws_reset();
        fake_ws.fail_allocation = allocation;
        CHECK(gs232b_esp_ws_open(&f.ws, &f.dispatcher) == GS232B_ERROR_RESOURCE_EXHAUSTED);
        CHECK(fake_ws.live_semaphores == 0 && !fake_ws.started);
    }
    return true;
}

static bool rx_overflow_send_failure_and_epoch(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(ws_command(&f, 0, "R\r", "\r"));
    uint8_t bytes[16384];
    memset(bytes, ' ', sizeof(bytes));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, true, bytes, sizeof(bytes)) == ESP_OK);
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, true, "L\r", 2) == ESP_FAIL);
    CHECK(ws_poll(&f, 1));
    CHECK(stopped(&f.sim));
    CHECK(ws_expect(0, HTTPD_WS_TYPE_CLOSE, (const uint8_t[]){3, 240}, 2));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(ws_connect(&f, 1, WS_STA_IP));
    CHECK(ws_command(&f, 0, "R\r", "\r"));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, true, "L\r", 2) == ESP_OK);
    gs232b_command_context_t stop_context;
    CHECK(gs232b_dispatcher_capture(&f.dispatcher, ws_client(&f, 1)->session, &stop_context) == GS232B_OK);
    CHECK(gs232b_dispatcher_feed(&f.dispatcher, stop_context, (const uint8_t *)"S\r", 2) == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(gs232b_esp_ws_service(&f.ws) == GS232B_ERROR_CONFLICT);
    CHECK(stopped(&f.sim));
    CHECK(ws_poll(&f, 1));
    CHECK(ws_expect_bytes(0, ""));
    CHECK(ws_expect_bytes(1, "\r"));
    CHECK(ws_command(&f, 0, "R\r", "\r"));
    fake_ws.sockets[0].send_error = EPIPE;
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, true, "C\r", 2) == ESP_OK);
    CHECK(ws_poll(&f, 2));
    CHECK(!fake_ws.sockets[0].active && fake_ws.sockets[1].active);
    CHECK(stopped(&f.sim));
    return ws_close(&f);
}

static bool delayed_httpd_does_not_delay_owner_cleanup(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(ws_command(&f, 0, "R\r", "\r"));
    const gs232b_session_key_t owner = ws_client(&f, 0)->session;
    gs232b_session_io_t io = gs232b_esp_ws_io(&f.ws);
    CHECK(gs232b_session_send(&io, owner, (const uint8_t *)"data", 4) == GS232B_OK);
    CHECK(gs232b_esp_ws_service(&f.ws) == GS232B_OK);
    /* NOTE: 故意不执行已排队 HTTPD work，模拟 HTTPD 正被慢 RX 占用。 */
    fake_ws.now_us += GS232B_WS_SEND_TIMEOUT_US;
    CHECK(gs232b_esp_ws_service(&f.ws) == GS232B_OK);
    CHECK(stopped(&f.sim));
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(fake_ws.sockets[0].active);
    fake_ws_run_work();
    CHECK(!fake_ws.sockets[0].active);
    CHECK(ws_poll(&f, 1));
    CHECK(ws_expect_bytes(0, ""));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(ws_command(&f, 0, "R\r", "\r"));
    fake_ws.now_us += GS232B_WS_PONG_TIMEOUT_US;
    CHECK(gs232b_esp_ws_service(&f.ws) == GS232B_OK);
    CHECK(stopped(&f.sim));
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    return ws_close(&f);
}

static bool handshake_and_sdk_failures_release_resources(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    fake_ws.sockets[0].bind_error = true;
    CHECK(fake_ws_connect(0, WS_ETH_IP) == ESP_FAIL);
    CHECK(ws_client(&f, 0) == NULL && fake_ws.sockets[0].bind_calls == 0);
    CHECK(ws_poll(&f, 1));
    fake_ws.sockets[0].bind_error = false;
    fake_ws.control_step = NULL;
    CHECK(fake_ws_connect(0, WS_ETH_IP) == ESP_FAIL);
    CHECK(ws_poll(&f, 1));
    fake_ws.control_step = ws_control_step;
    fake_ws.fail_override = true;
    CHECK(fake_ws_connect(0, WS_ETH_IP) == ESP_FAIL);
    CHECK(ws_poll(&f, 1));
    fake_ws.fail_override = false;
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(ws_command(&f, 0, "R\r", "\r"));
    fake_ws.fail_encode = true;
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, true, "C\r", 2) == ESP_OK);
    CHECK(ws_poll(&f, 2));
    CHECK(!fake_ws.sockets[0].active && stopped(&f.sim));
    fake_ws.fail_encode = false;
    CHECK(ws_close(&f));
    for (unsigned failure = 0; failure < 3; ++failure) {
        CHECK(ws_init(&f));
        CHECK(ws_close(&f));
        fake_ws.fail_start = failure == 0;
        fake_ws.fail_register = failure != 0;
        fake_ws.fail_stop = failure == 2;
        CHECK(gs232b_esp_ws_open(&f.ws, &f.dispatcher) == GS232B_ERROR_IO);
        if (fake_ws.fail_stop) {
            CHECK(fake_ws.started && fake_ws.live_semaphores == 5);
            fake_ws.fail_stop = false;
            CHECK(ws_close(&f));
        }
        CHECK(fake_ws.live_semaphores == 0 && !fake_ws.started);
    }
    return true;
}

static bool final_short_write_with_lock_contention(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    const gs232b_session_io_t io = gs232b_esp_ws_io(&f.ws);
    CHECK(gs232b_session_send(&io, ws_client(&f, 0)->session, (const uint8_t *)"x", 1) == GS232B_OK);
    fake_ws.sockets[0].send_limit = 2;
    CHECK(ws_poll(&f, 1));
    CHECK(ws_client(&f, 0)->wire_sent == 2);
    CHECK(gs232b_esp_ws_service(&f.ws) == GS232B_OK);
    CHECK(xSemaphoreTake(f.ws.lock, 0) == pdPASS);
    fake_ws_run_work();
    CHECK(xSemaphoreGive(f.ws.lock) == pdPASS);
    CHECK(ws_poll(&f, 1));
    CHECK(!atomic_load(&ws_client(&f, 0)->send_pending));
    CHECK(ws_expect_bytes(0, "x"));
    fake_ws.now_us += GS232B_WS_SEND_TIMEOUT_US;
    CHECK(ws_poll(&f, 1));
    CHECK(fake_ws.sockets[0].active);
    return ws_close(&f);
}

bool ws_fault_tests(void)
{
    return bounded_tx_short_writes_and_stall() && disconnect_reuse_and_queued_input() &&
        admission_interfaces_and_resource_cleanup() && rx_overflow_send_failure_and_epoch() &&
        delayed_httpd_does_not_delay_owner_cleanup() && handshake_and_sdk_failures_release_resources() &&
        final_short_write_with_lock_contention();
}
