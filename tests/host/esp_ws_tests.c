#include "ws_test_support.h"
#include "lwip/sockets.h"

bool ws_boundary_tests(void);
bool ws_fault_tests(void);
bool ws_network_tests(void);
bool ws_watchdog_tests(void);

static bool cross_message_and_multiple_commands(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(strcmp(fake_ws.uri.uri, "/ws/gs232b") == 0);
    CHECK(fake_ws.uri.is_websocket && fake_ws.uri.handle_ws_control_frames);
    CHECK(fake_ws.config.server_port == 80 && !fake_ws.config.lru_purge_enable);
    CHECK(fake_ws.config.keep_alive_enable && fake_ws.config.max_open_sockets == 4);
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(fake_ws.handlers == 0 && fake_ws.hooks == 2);
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, true, "C", 1) == ESP_OK);
    CHECK(ws_poll(&f, 2));
    CHECK(ws_expect_bytes(0, ""));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, true, "2\r\nB\rN\r", 7) == ESP_OK);
    CHECK(ws_poll(&f, 8));
    CHECK(ws_expect_bytes(0, "AZ=090  EL=090\r\nEL=090\r\n=0000=0000\r\n"));
    CHECK(ws_connect(&f, 1, WS_STA_IP));
    CHECK(ws_command(&f, 1, "C\r", "AZ=090\r\n"));
    CHECK(ws_expect_bytes(0, ""));
    CHECK(f.dispatcher.control.sessions[0].transport == GS232B_TRANSPORT_ETHERNET_WS);
    CHECK(f.dispatcher.control.sessions[1].transport == GS232B_TRANSPORT_WIFI_WS);
    return ws_close(&f);
}

static bool fragmented_with_control_frames(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, false, "C", 1) == ESP_OK);
    CHECK(ws_poll(&f, 1));
    const gs232b_control_session_t before = f.dispatcher.control.sessions[0];
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_PING, true, "R\r", 2) == ESP_OK);
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_PONG, true, "L\r", 2) == ESP_OK);
    CHECK(memcmp(&before, &f.dispatcher.control.sessions[0], sizeof(before)) == 0);
    CHECK(ws_poll(&f, 1));
    CHECK(ws_expect(0, HTTPD_WS_TYPE_PONG, "R\r", 2));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_CONTINUE, false, "2", 1) == ESP_OK);
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_PING, true, NULL, 0) == ESP_OK);
    CHECK(ws_poll(&f, 2));
    CHECK(ws_expect(0, HTTPD_WS_TYPE_PONG, "", 0));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_CONTINUE, true, "\rC\r", 3) == ESP_OK);
    CHECK(ws_poll(&f, 8));
    CHECK(ws_expect_bytes(0, "AZ=090  EL=090\r\nAZ=090\r\n"));
    CHECK(stopped(&f.sim));
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(ws_close(&f));
    return true;
}

static bool zero_frames_and_invalid_fragments(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, false, NULL, 0) == ESP_OK);
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_CONTINUE, true, NULL, 0) == ESP_OK);
    CHECK(ws_command(&f, 0, "C\r", "AZ=090\r\n"));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_CONTINUE, true, "R\r", 2) == ESP_FAIL);
    CHECK(ws_poll(&f, 1));
    const uint8_t protocol_error[] = {3, 234};
    CHECK(ws_expect(0, HTTPD_WS_TYPE_CLOSE, protocol_error, sizeof(protocol_error)));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, false, "C", 1) == ESP_OK);
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_TEXT, true, "R\r", 2) == ESP_FAIL);
    CHECK(ws_poll(&f, 1));
    CHECK(ws_expect(0, HTTPD_WS_TYPE_CLOSE, protocol_error, sizeof(protocol_error)));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_PING, false, NULL, 0) == ESP_FAIL);
    CHECK(ws_poll(&f, 1));
    CHECK(ws_expect(0, HTTPD_WS_TYPE_CLOSE, protocol_error, sizeof(protocol_error)));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    uint8_t large_ping[126] = {0};
    CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_PING, true, large_ping, sizeof(large_ping)) == ESP_FAIL);
    CHECK(ws_poll(&f, 1));
    CHECK(ws_expect(0, HTTPD_WS_TYPE_CLOSE, protocol_error, sizeof(protocol_error)));
    return ws_close(&f);
}

static bool dual_stack_ipv4_mapped_admission(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    fake_ws.sockets[0].address_family = AF_INET6;
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(ws_client(&f, 0)->transport == GS232B_TRANSPORT_ETHERNET_WS);
    CHECK(ws_command(&f, 0, "C\r", "AZ=090\r\n"));
    fake_ws.sockets[1].address_family = AF_INET6;
    CHECK(ws_connect(&f, 1, WS_STA_IP));
    CHECK(ws_client(&f, 1)->transport == GS232B_TRANSPORT_WIFI_WS);
    fake_ws.sockets[2].address_family = AF_INET6;
    fake_ws.sockets[2].native_ipv6 = true;
    CHECK(fake_ws_connect(2, WS_ETH_IP) == ESP_FAIL);
    CHECK(gs232b_esp_ws_interface(&f.ws, GS232B_TRANSPORT_WIFI_WS,
        WS_ETH_IP, 2, WS_STA_IF_NAME) == GS232B_OK);
    CHECK(fake_ws_connect(2, WS_ETH_IP) == ESP_FAIL);
    CHECK(ws_poll(&f, 2));
    CHECK(fake_ws.sockets[0].active && !fake_ws.sockets[1].active);
    return ws_close(&f);
}

int main(void)
{
    return cross_message_and_multiple_commands() && fragmented_with_control_frames() &&
        zero_frames_and_invalid_fragments() && dual_stack_ipv4_mapped_admission() &&
        ws_boundary_tests() && ws_fault_tests() && ws_network_tests() &&
        ws_watchdog_tests() ? EXIT_SUCCESS : EXIT_FAILURE;
}
