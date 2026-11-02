#include "wifi_sta_test_support.h"

static bool static_and_dhcp(void)
{
    gs232b_wifi_sta_config_t config = wifi_good;
    config.mode = GS232B_WIFI_STA_STATIC;
    config.ipv4 = (gs232b_wifi_sta_address_t){0xc0a81432, 0xffffff00, 0xc0a81401};
    CHECK(gs232b_wifi_sta_config_valid(&config));
    wifi_fixture_t f;
    CHECK(wifi_open(&f, &config));
    esp_netif_dhcp_status_t dhcp;
    CHECK(esp_netif_dhcpc_get_status(f.sta.netif, &dhcp) == ESP_OK && dhcp != ESP_NETIF_DHCP_STARTED);
    fake_wifi_connected();
    gs232b_wifi_sta_snapshot_t snapshot;
    CHECK(wifi_snapshot(&f, &snapshot));
    CHECK(snapshot.state == GS232B_WIFI_STA_READY && snapshot.ipv4_valid);
    CHECK(snapshot.current.address == config.ipv4.address && snapshot.current.netmask == config.ipv4.netmask);
    CHECK(snapshot.current.gateway == config.ipv4.gateway && snapshot.current.dns[0] == 0);
    const uint64_t epoch = snapshot.session_epoch;
    config.mode = GS232B_WIFI_STA_DHCP;
    CHECK(gs232b_esp_wifi_sta_configure(&f.sta, &config) == GS232B_OK && wifi_settle(&f));
    fake_wifi_connected();
    CHECK(wifi_snapshot(&f, &snapshot));
    CHECK(snapshot.state == GS232B_WIFI_STA_DHCP_WAIT && !snapshot.ipv4_valid);
    fake_wifi_lease(&f.sta, wifi_ip);
    CHECK(wifi_snapshot(&f, &snapshot));
    CHECK(snapshot.state == GS232B_WIFI_STA_READY && snapshot.current.address == wifi_ip.address);
    CHECK(snapshot.session_epoch > epoch);
    CHECK(wifi_close(&f));
    config.mode = GS232B_WIFI_STA_STATIC;
    config.ipv4.gateway = 0;
    CHECK(gs232b_wifi_sta_config_valid(&config));
    config.ipv4.netmask = 0xfffffffe;
    CHECK(!gs232b_wifi_sta_config_valid(&config));
    return true;
}

static bool open_and_scan(void)
{
    const gs232b_wifi_sta_config_t open = {.enabled = true, .ssid = "open network"};
    wifi_fixture_t f;
    CHECK(wifi_open(&f, &open));
    CHECK(fake_wifi_status().credentials.sta.threshold.authmode == WIFI_AUTH_OPEN);
    fake_wifi_connected(); fake_wifi_lease(&f.sta, wifi_ip);
    gs232b_wifi_sta_snapshot_t snapshot;
    CHECK(wifi_snapshot(&f, &snapshot));
    CHECK(snapshot.state == GS232B_WIFI_STA_READY && !snapshot.password_set);
    const gs232b_provisioning_ap_config_t ap = {.ssid = "setup", .password = "setup-pass",
        .address = 0xc0a80401, .netmask = 0xffffff00, .gateway = 0xc0a80401};
    CHECK(gs232b_esp_wifi_sta_ap_start(&f.sta, &ap) == GS232B_OK && wifi_settle(&f));
    wifi_ready(&f);
    CHECK(wifi_snapshot(&f, &snapshot) && snapshot.ipv4_valid);
    const uint64_t epoch = snapshot.session_epoch;
    const wifi_ap_record_t records[] = {
        {.ssid = "中文网络", .rssi = -42, .authmode = WIFI_AUTH_WPA2_PSK},
        {.ssid = "quote\"\\\n", .rssi = -70, .authmode = WIFI_AUTH_OPEN},
        {.ssid = "\xc0\xaf", .rssi = -80, .authmode = WIFI_AUTH_OPEN},
    };
    fake_wifi_scan_results(records, 3);
    gs232b_provisioning_ap_t networks[GS232B_PROVISIONING_SCAN_MAX_APS];
    size_t count = 0; bool truncated = false;
    CHECK(gs232b_esp_wifi_sta_ap_scan(&f.sta, networks, 64, &count, &truncated) == GS232B_OK);
    CHECK(count == 2 && !truncated);
    CHECK(strcmp(networks[0].ssid, "中文网络") == 0);
    CHECK(strcmp(networks[1].ssid, "quote\"\\\n") == 0 && networks[1].auth == WIFI_AUTH_OPEN);
    fake_wifi_scan_results(records, 3);
    const unsigned scans = fake_wifi_status().calls[WIFI_FAKE_SCAN_START];
    fake_wifi_scan_pause(true);
    CHECK(gs232b_esp_wifi_sta_ap_scan_async(&f.sta, networks, 64, &count, &truncated) ==
        GS232B_ERROR_WOULD_BLOCK);
    const uint64_t deadline = wifi_wall_ms() + 3000;
    while (!fake_wifi_scan_paused() && wifi_wall_ms() < deadline) sched_yield();
    CHECK(fake_wifi_scan_paused());
    CHECK(gs232b_esp_wifi_sta_interfaces_stable(&f.sta));
    fake_wifi_disconnected(WIFI_REASON_AUTH_FAIL);
    CHECK(gs232b_esp_wifi_sta_service(&f.sta) == GS232B_ERROR_IO);
    CHECK(gs232b_esp_wifi_sta_snapshot(&f.sta, &snapshot) == GS232B_OK);
    CHECK(!snapshot.ipv4_valid && snapshot.session_epoch > epoch &&
        snapshot.state == GS232B_WIFI_STA_RETRY_WAIT);
    CHECK(gs232b_esp_wifi_sta_ap_scan_async(&f.sta, networks, 64, &count, &truncated) ==
        GS232B_ERROR_WOULD_BLOCK);
    fake_wifi_scan_pause(false);
    CHECK(wifi_settle(&f));
    CHECK(gs232b_esp_wifi_sta_ap_scan_async(&f.sta, networks, 64, &count, &truncated) == GS232B_OK);
    CHECK(count == 2 && !truncated && fake_wifi_status().calls[WIFI_FAKE_SCAN_START] == scans + 1U);
    wifi_ap_record_t many[80] = {0};
    for (unsigned i = 0; i < 80; ++i) {
        memset(many[i].ssid, '\x1f', 31);
        many[i].ssid[31] = 'A' + i % 26;
        many[i].rssi = -100 + (int)i;
    }
    fake_wifi_scan_results(many, 80);
    CHECK(gs232b_esp_wifi_sta_ap_scan(&f.sta, networks, 64, &count, &truncated) == GS232B_OK);
    CHECK(count == 64 && truncated && networks[0].rssi == -21 && networks[63].rssi == -84);
    const fake_wifi_call_t failures[] = {WIFI_FAKE_SCAN_COUNT, WIFI_FAKE_SCAN_RECORD, WIFI_FAKE_SCAN_CLEAR};
    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        const unsigned clears = fake_wifi_status().calls[WIFI_FAKE_SCAN_CLEAR];
        fake_wifi_fail(failures[i], ESP_ERR_NO_MEM);
        CHECK(gs232b_esp_wifi_sta_ap_scan(&f.sta, networks, 64, &count, &truncated) ==
            GS232B_ERROR_RESOURCE_EXHAUSTED);
        CHECK(count == 0 && !truncated);
        CHECK(fake_wifi_status().calls[WIFI_FAKE_SCAN_CLEAR] == clears + 1U);
    }
    fake_wifi_scan_results(many, 0);
    CHECK(gs232b_esp_wifi_sta_ap_scan(&f.sta, networks, 64, &count, &truncated) == GS232B_OK);
    CHECK(count == 0 && !truncated);
    CHECK(wifi_close(&f));
    CHECK(wifi_open(&f, NULL));
    CHECK(gs232b_esp_wifi_sta_ap_start(&f.sta, &ap) == GS232B_OK && wifi_settle(&f));
    CHECK(fake_wifi_status().mode == WIFI_MODE_APSTA);
    CHECK(fake_wifi_status().calls[WIFI_FAKE_CONNECT] == 0);
    fake_wifi_scan_results(records, 3);
    CHECK(gs232b_esp_wifi_sta_ap_scan(&f.sta, networks, 64, &count, &truncated) == GS232B_OK);
    CHECK(count == 2 && !truncated);
    CHECK(wifi_close(&f));
    return true;
}

bool wifi_test_ipv4(void)
{
    return static_and_dhcp() && open_and_scan();
}
