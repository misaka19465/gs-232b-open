#include "ws_test_support.h"
#include "gs232b/esp_ethernet.h"
#include "gs232b/esp_wifi_sta.h"

static void publish_duplicate_wifi_address(void *ctx)
{
    gs232b_esp_ws_t *adapter = ctx;
    const gs232b_error_t result = gs232b_esp_ws_interface(adapter,
        GS232B_TRANSPORT_WIFI_WS, WS_ETH_IP, 2, WS_STA_IF_NAME);
    if (result != GS232B_OK) { abort(); }
}

static bool admission_revalidates_binding(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(gs232b_esp_ws_interface(&f.ws, GS232B_TRANSPORT_ETHERNET_WS,
        WS_ETH_IP, 1, "abcdef") == GS232B_ERROR_INVALID_ARGUMENT);

    fake_ws.bind_step = publish_duplicate_wifi_address;
    fake_ws.bind_ctx = &f.ws;
    CHECK(fake_ws_connect(0, WS_ETH_IP) == ESP_FAIL);
    CHECK(ws_client(&f, 0) == NULL && fake_ws.sockets[0].bind_calls == 1);
    CHECK(ws_poll(&f, 1));
    fake_ws.bind_step = NULL;
    CHECK(gs232b_esp_ws_interface(&f.ws, GS232B_TRANSPORT_WIFI_WS,
        WS_STA_IP, 3, WS_STA_IF_NAME) == GS232B_OK);

    fake_ws.sockets[1].bind_error = true;
    CHECK(fake_ws_connect(1, WS_STA_IP) == ESP_FAIL);
    CHECK(ws_client(&f, 1) == NULL && fake_ws.sockets[1].bind_calls == 0);
    fake_ws.sockets[1].bind_error = false;
    CHECK(ws_connect(&f, 1, WS_STA_IP));
    CHECK(strcmp(fake_ws.sockets[1].bound_interface, WS_STA_IF_NAME) == 0);
    return ws_close(&f);
}

static bool interface_changes_and_default_route_failover_are_local(void)
{
    ws_fixture_t f;
    CHECK(ws_init(&f));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));
    CHECK(ws_connect(&f, 1, WS_STA_IP));
    CHECK(strcmp(fake_ws.sockets[0].bound_interface, WS_ETH_IF_NAME) == 0);
    CHECK(strcmp(fake_ws.sockets[1].bound_interface, WS_STA_IF_NAME) == 0);
    CHECK(ws_client(&f, 0)->interface.address == WS_ETH_IP);
    CHECK(ws_client(&f, 1)->interface.address == WS_STA_IP);
    CHECK(GS232B_ESP_ETHERNET_ROUTE_PRIORITY > GS232B_ESP_WIFI_STA_ROUTE_PRIORITY);

    CHECK(ws_command(&f, 1, "r\r", "\r"));
    CHECK(advance_ok(&f.sim, 15));
    const gs232b_session_key_t owner = ws_client(&f, 1)->session;
    const uint32_t control_epoch = f.dispatcher.control.epoch;
    CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, owner));
    CHECK(!stopped(&f.sim));

    fake_ws_disconnect(0);
    CHECK(ws_poll(&f, 2));
    CHECK(!fake_ws.sockets[0].active && fake_ws.sockets[1].active);
    CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, owner));
    CHECK(f.dispatcher.control.epoch == control_epoch);
    CHECK(!stopped(&f.sim));
    CHECK(ws_connect(&f, 0, WS_ETH_IP));

    const uint32_t changed_eth_ip = UINT32_C(0xc0a80103);
    CHECK(gs232b_esp_ws_interface(&f.ws, GS232B_TRANSPORT_ETHERNET_WS,
        changed_eth_ip, 2, WS_ETH_IF_NAME) == GS232B_OK);
    CHECK(ws_poll(&f, 2));
    CHECK(!fake_ws.sockets[0].active && fake_ws.sockets[1].active);
    CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, owner));
    CHECK(f.dispatcher.control.epoch == control_epoch);
    CHECK(!stopped(&f.sim));
    CHECK(ws_command(&f, 1, "C\r", "AZ=090\r\n"));

    CHECK(ws_connect(&f, 0, changed_eth_ip));
    CHECK(strcmp(fake_ws.sockets[0].bound_interface, WS_ETH_IF_NAME) == 0);
    CHECK(gs232b_esp_ws_interface(&f.ws, GS232B_TRANSPORT_ETHERNET_WS,
        0, 3, NULL) == GS232B_OK);
    CHECK(ws_poll(&f, 2));
    CHECK(!fake_ws.sockets[0].active && fake_ws.sockets[1].active);
    CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, owner));
    CHECK(f.dispatcher.control.epoch == control_epoch);
    CHECK(!stopped(&f.sim));

    const uint32_t restored_eth_ip = UINT32_C(0xc0a80104);
    CHECK(gs232b_esp_ws_interface(&f.ws, GS232B_TRANSPORT_ETHERNET_WS,
        restored_eth_ip, 4, WS_ETH_IF_NAME) == GS232B_OK);
    CHECK(ws_connect(&f, 0, restored_eth_ip));
    CHECK(fake_ws.sockets[1].active);
    CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, owner));
    CHECK(!stopped(&f.sim));

    CHECK(gs232b_esp_ws_interface(&f.ws, GS232B_TRANSPORT_WIFI_WS,
        UINT32_C(0xc0a80203), 2, WS_STA_IF_NAME) == GS232B_OK);
    CHECK(ws_poll(&f, 2));
    CHECK(fake_ws.sockets[0].active && !fake_ws.sockets[1].active);
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(stopped(&f.sim));
    return ws_close(&f);
}

bool ws_network_tests(void)
{
    return admission_revalidates_binding() &&
        interface_changes_and_default_route_failover_are_local();
}
