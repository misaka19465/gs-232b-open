#include "ws_test_support.h"

static bool transport_ping_and_pong(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    fake_ws.now_us += GS232B_WS_PING_US;
    CHECK(ws_poll(&f, 1));
    const uint8_t serial[] = {0, 0, 0, 1};
    CHECK(ws_expect(0, HTTPD_WS_TYPE_PING, serial, sizeof(serial)));
    const gs232b_control_session_t before = f.dispatcher.control.sessions[0];
    fake_ws.now_us += 1;
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_PONG, true, serial, sizeof(serial)) == ESP_OK);
    CHECK(memcmp(&before, &f.dispatcher.control.sessions[0], sizeof(before)) == 0);
    CHECK(ws_client(&f, 0)->last_pong_us == fake_ws.now_us);
    CHECK(ws_client(&f, 0)->rx_count == 0);
    fake_ws.now_us += GS232B_WS_PING_US;
    CHECK(ws_poll(&f, 1));
    CHECK(ws_expect(0, HTTPD_WS_TYPE_PING, (const uint8_t[]){0, 0, 0, 2}, 4));
    const uint64_t last = ws_client(&f, 0)->last_pong_us;
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_PONG, true, serial, sizeof(serial)) == ESP_OK);
    CHECK(ws_client(&f, 0)->last_pong_us == last);
    fake_ws.now_us = last + GS232B_WS_PONG_TIMEOUT_US;
    CHECK(ws_poll(&f, 2));
    CHECK(!fake_ws.sockets[0].active);
    return ws_close(&f);
}

static bool pong_never_renews_application_watchdog(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(ws_connect(&f, 1, WS_STA_IP));
    CHECK(ws_command(&f, 0, "R\r", "\r"));
    CHECK(ws_command(&f, 0, "!SESSION WATCHDOG {\"timeout_ms\":6000}\r",
        "!{\"ok\":true,\"cmd\":\"SESSION WATCHDOG\",\"data\":{\"timeout_ms\":6000,\"enabled\":true}}\r\n"));
    CHECK(gs232b_dispatcher_watchdog_set(&f.dispatcher, ws_client(&f, 1)->session, 6000) ==
        GS232B_ERROR_NOT_OWNER);
    const gs232b_time_us_t renewed = f.dispatcher.watchdog_renewed_us;
    f.sim.now_us += GS232B_WS_PING_US;
    fake_ws.now_us = f.sim.now_us;
    CHECK(ws_poll(&f, 1));
    CHECK(ws_expect(0, HTTPD_WS_TYPE_PING, (const uint8_t[]){0, 0, 0, 1}, 4));
    CHECK(ws_expect(1, HTTPD_WS_TYPE_PING, (const uint8_t[]){0, 0, 0, 1}, 4));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_PONG, true, (const uint8_t[]){0, 0, 0, 1}, 4) == ESP_OK);
    CHECK(ws_client(&f, 0)->last_pong_us == f.sim.now_us);
    CHECK(f.dispatcher.watchdog_renewed_us == renewed);
    CHECK(ws_command(&f, 1, "C\r", "AZ=090\r\n"));
    CHECK(f.dispatcher.watchdog_renewed_us == renewed);
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, true, "?\r", 2) == ESP_OK);
    CHECK(gs232b_esp_ws_service(&f.ws) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(ws_poll(&f, 2));
    CHECK(ws_expect_bytes(0, "?>\r\n"));
    CHECK(f.dispatcher.watchdog_renewed_us == renewed);
    f.sim.now_us = renewed + 6000000;
    fake_ws.now_us = f.sim.now_us;
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_ERROR_TIMEOUT);
    CHECK(stopped(&f.sim));
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(ws_poll(&f, 2));
    CHECK(!fake_ws.sockets[0].active && fake_ws.sockets[1].active);
    return ws_close(&f);
}

static bool valid_commands_and_application_ping_renew(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(ws_command(&f, 0, "R\r", "\r"));
    const gs232b_session_key_t owner = ws_client(&f, 0)->session;
    CHECK(gs232b_dispatcher_watchdog_set(&f.dispatcher, owner, 999) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(ws_command(&f, 0, "!SESSION WATCHDOG {\"timeout_ms\":1000}\r",
        "!{\"ok\":true,\"cmd\":\"SESSION WATCHDOG\",\"data\":{\"timeout_ms\":1000,\"enabled\":true}}\r\n"));
    f.sim.now_us += 500000;
    fake_ws.now_us = f.sim.now_us;
    CHECK(ws_command(&f, 0, "C\r", "AZ=090\r\n"));
    CHECK(f.dispatcher.watchdog_renewed_us == f.sim.now_us);
    f.sim.now_us += 500000;
    CHECK(ws_command(&f, 0, "!SESSION PING\r",
        "!{\"ok\":true,\"cmd\":\"SESSION PING\",\"data\":{\"timeout_ms\":1000,\"enabled\":true}}\r\n"));
    CHECK(f.dispatcher.watchdog_renewed_us == f.sim.now_us);
    CHECK(gs232b_dispatcher_watchdog_set(&f.dispatcher, owner, 0) == GS232B_OK);
    f.sim.now_us += 1000001;
    CHECK(ws_poll(&f, 1));
    CHECK(gs232b_session_key_equal(owner, f.dispatcher.control.owner));
    return ws_close(&f);
}

static bool partial_timeout_and_expired_application_ping(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, true, "C", 1) == ESP_OK);
    CHECK(ws_poll(&f, 1));
    const uint64_t last_byte = f.dispatcher.control.sessions[0].parser.last_byte_at_us;
    f.sim.now_us += 1500000;
    fake_ws.now_us = f.sim.now_us;
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_PONG, true, "\rR\r", 3) == ESP_OK);
    CHECK(f.dispatcher.control.sessions[0].parser.last_byte_at_us == last_byte);
    CHECK(f.dispatcher.control.sessions[0].parser.length == 1);
    f.sim.now_us = last_byte + GS232B_LINE_PARTIAL_TIMEOUT_US;
    fake_ws.now_us = f.sim.now_us;
    CHECK(ws_poll(&f, 1));
    CHECK(ws_expect_bytes(0, "?>\r\n"));
    CHECK(ws_command(&f, 0, "\rR\r", "\r"));
    const gs232b_session_key_t owner = ws_client(&f, 0)->session;
    CHECK(gs232b_dispatcher_watchdog_set(&f.dispatcher, owner, 1000) == GS232B_OK);
    f.sim.now_us += 1000000;
    CHECK(gs232b_dispatcher_watchdog_ping(&f.dispatcher, owner) == GS232B_ERROR_TIMEOUT);
    CHECK(stopped(&f.sim));
    return ws_close(&f);
}

bool ws_watchdog_tests(void)
{
    return transport_ping_and_pong() && pong_never_renews_application_watchdog() &&
        valid_commands_and_application_ping_renew() && partial_timeout_and_expired_application_ping();
}
