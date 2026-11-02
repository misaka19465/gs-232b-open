#include "wifi_sta_test_support.h"

bool wifi_test_ipv4(void);

static bool validation(void)
{
    CHECK(!gs232b_wifi_sta_config_valid(NULL));
    CHECK(gs232b_wifi_sta_config_valid(&wifi_good));
    gs232b_wifi_sta_config_t c = {0};
    CHECK(gs232b_wifi_sta_config_valid(&c));
    c.enabled = true; CHECK(!gs232b_wifi_sta_config_valid(&c));
    c = wifi_good; c.password[0] = 0; CHECK(gs232b_wifi_sta_config_valid(&c));
    c = wifi_good; strcpy(c.password, "1234567"); CHECK(!gs232b_wifi_sta_config_valid(&c));
    strcpy(c.password, "12345678"); CHECK(gs232b_wifi_sta_config_valid(&c));
    memset(c.password, 'p', 63); c.password[63] = 0; CHECK(gs232b_wifi_sta_config_valid(&c));
    memset(c.password, 'a', 64); c.password[64] = 0; CHECK(gs232b_wifi_sta_config_valid(&c));
    c.password[63] = 'g'; CHECK(!gs232b_wifi_sta_config_valid(&c));
    c = wifi_good; memset(c.ssid, 'x', 32); c.ssid[32] = 0; CHECK(gs232b_wifi_sta_config_valid(&c));
    c.ssid[31] = (char)0xc3; CHECK(!gs232b_wifi_sta_config_valid(&c));
    const char *bad_utf8[] = {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\x80", "\xe0\x80\x80"};
    for (unsigned i = 0; i < sizeof(bad_utf8) / sizeof(bad_utf8[0]); ++i) {
        c = wifi_good; strcpy(c.ssid, bad_utf8[i]); CHECK(!gs232b_wifi_sta_config_valid(&c));
    }
    c = wifi_good; memset(c.ssid, 'a', sizeof(c.ssid)); CHECK(!gs232b_wifi_sta_config_valid(&c));
    c = wifi_good; memset(c.password, 'a', sizeof(c.password)); CHECK(!gs232b_wifi_sta_config_valid(&c));
    CHECK(gs232b_wifi_sta_retry_delay(0) == 0);
    CHECK(gs232b_wifi_sta_retry_delay(UINT32_MAX) == GS232B_WIFI_STA_RETRY_MAX_US);
    return true;
}

static bool wrong_password_and_backoff(void)
{
    wifi_fixture_t f; CHECK(wifi_open(&f, &wifi_good));
    const uint64_t delays[] = {1, 2, 4, 8, 16, 30, 30, 30};
    for (unsigned i = 0; i < sizeof(delays) / sizeof(delays[0]); ++i) {
        fake_wifi_disconnected(i % 2 == 0 ? WIFI_REASON_AUTH_FAIL : WIFI_REASON_HANDSHAKE_TIMEOUT);
        gs232b_wifi_sta_snapshot_t s; CHECK(wifi_snapshot(&f, &s));
        CHECK(s.state == GS232B_WIFI_STA_RETRY_WAIT && s.failure == GS232B_WIFI_STA_FAILURE_AUTH);
        CHECK(s.disconnect_reason == (i % 2 == 0 ? WIFI_REASON_AUTH_FAIL : WIFI_REASON_HANDSHAKE_TIMEOUT));
        CHECK(s.consecutive_failures == i + 1 && s.retry_delay_us == delays[i] * UINT64_C(1000000));
        CHECK(s.retry_at_us == (uint64_t)esp_timer_get_time() + s.retry_delay_us);
        const unsigned calls = fake_wifi_status().calls[WIFI_FAKE_CONNECT];
        fake_wifi_disconnected(WIFI_REASON_AUTH_FAIL);
        fake_eth_advance(s.retry_delay_us - 1);
        CHECK(gs232b_esp_wifi_sta_service(&f.sta) == GS232B_ERROR_IO);
        CHECK(fake_wifi_status().calls[WIFI_FAKE_CONNECT] == calls);
        fake_eth_advance(1);
        CHECK(gs232b_esp_wifi_sta_service(&f.sta) == GS232B_OK);
        CHECK(wifi_settle(&f));
        CHECK(fake_wifi_status().calls[WIFI_FAKE_CONNECT] == calls + 1);
        CHECK(memcmp(f.sta.configured.password, wifi_good.password, sizeof(wifi_good.password)) == 0);
        CHECK(wifi_snapshot(&f, &s));
        CHECK(s.state == GS232B_WIFI_STA_CONNECTING && s.consecutive_failures == i + 1);
    }
    fake_wifi_connected();
    gs232b_wifi_sta_snapshot_t s; CHECK(wifi_snapshot(&f, &s));
    CHECK(s.state == GS232B_WIFI_STA_DHCP_WAIT && !s.ipv4_valid && s.consecutive_failures == 8);
    fake_wifi_lease(&f.sta, wifi_ip);
    CHECK(wifi_snapshot(&f, &s));
    CHECK(s.state == GS232B_WIFI_STA_READY && s.consecutive_failures == 0 && s.retry_at_us == 0);
    const uint64_t epoch = s.session_epoch;
    fake_wifi_disconnected(WIFI_REASON_BEACON_TIMEOUT);
    CHECK(wifi_snapshot(&f, &s));
    CHECK(s.failure == GS232B_WIFI_STA_FAILURE_DISCONNECTED && s.disconnect_reason == WIFI_REASON_BEACON_TIMEOUT);
    CHECK(s.consecutive_failures == 1 && s.session_epoch > epoch && !s.connected && !s.ipv4_valid);
    CHECK(wifi_close(&f));
    return true;
}

static bool timeout_and_ip_events(void)
{
    wifi_fixture_t f; CHECK(wifi_open(&f, &wifi_good));
    fake_eth_advance(GS232B_WIFI_STA_CONNECT_TIMEOUT_US - 1);
    CHECK(gs232b_esp_wifi_sta_service(&f.sta) == GS232B_OK);
    fake_eth_advance(1);
    CHECK(gs232b_esp_wifi_sta_service(&f.sta) == GS232B_ERROR_TIMEOUT);
    gs232b_wifi_sta_snapshot_t s; CHECK(wifi_snapshot(&f, &s));
    CHECK(s.failure == GS232B_WIFI_STA_FAILURE_CONNECT_TIMEOUT);
    fake_eth_advance(s.retry_delay_us); CHECK(wifi_settle(&f));
    fake_wifi_connected();
    fake_eth_advance(GS232B_WIFI_STA_DHCP_TIMEOUT_US);
    CHECK(gs232b_esp_wifi_sta_service(&f.sta) == GS232B_ERROR_TIMEOUT);
    CHECK(wifi_snapshot(&f, &s));
    CHECK(s.failure == GS232B_WIFI_STA_FAILURE_DHCP_TIMEOUT && s.consecutive_failures == 2);
    fake_eth_advance(s.retry_delay_us); CHECK(wifi_settle(&f)); wifi_ready(&f);
    CHECK(wifi_snapshot(&f, &s));
    CHECK(s.current.dns[0] == wifi_ip.dns[0] && s.current.dns[1] == wifi_ip.dns[1]);
    const uint64_t epoch = s.session_epoch;
    const ip_event_got_ip_t foreign = {.esp_netif = (esp_netif_t *)(uintptr_t)0x1234};
    CHECK(esp_event_post(IP_EVENT, IP_EVENT_STA_GOT_IP, &foreign, sizeof(foreign), 0) == ESP_OK);
    const ip_event_got_ip_t old = {.esp_netif = f.sta.netif};
    CHECK(esp_event_post(IP_EVENT, IP_EVENT_STA_LOST_IP, &old, sizeof(old), 0) == ESP_OK);
    CHECK(esp_event_post(IP_EVENT, IP_EVENT_STA_GOT_IP, &old, sizeof(old), 0) == ESP_OK);
    fake_eth_flush(); CHECK(wifi_snapshot(&f, &s));
    CHECK(s.state == GS232B_WIFI_STA_READY && s.session_epoch == epoch);
    gs232b_wifi_sta_ipv4_t renewed = wifi_ip; ++renewed.address;
    fake_wifi_lease(&f.sta, renewed); CHECK(wifi_snapshot(&f, &s));
    CHECK(s.session_epoch > epoch && s.current.address == renewed.address);
    const uint64_t new_epoch = s.session_epoch;
    fake_wifi_lost_ip(&f.sta); CHECK(wifi_snapshot(&f, &s));
    CHECK(s.state == GS232B_WIFI_STA_DHCP_WAIT && s.connected && !s.ipv4_valid && s.session_epoch > new_epoch);
    fake_wifi_lease(&f.sta, wifi_ip); CHECK(wifi_snapshot(&f, &s));
    CHECK(s.state == GS232B_WIFI_STA_READY);
    CHECK(wifi_close(&f));
    return true;
}

static bool disable_and_reconfigure(void)
{
    wifi_fixture_t f; CHECK(wifi_open(&f, &wifi_good));
    fake_wifi_disconnected(WIFI_REASON_NO_AP_FOUND);
    gs232b_wifi_sta_snapshot_t s; CHECK(wifi_snapshot(&f, &s));
    CHECK(s.failure == GS232B_WIFI_STA_FAILURE_NO_AP);
    CHECK(gs232b_esp_wifi_sta_set_enabled(&f.sta, false) == GS232B_OK);
    CHECK(wifi_settle(&f)); CHECK(wifi_snapshot(&f, &s));
    CHECK(!s.enabled && s.password_set && s.state == GS232B_WIFI_STA_DISABLED && f.sta.netif == NULL);
    const unsigned calls = fake_wifi_status().calls[WIFI_FAKE_CONNECT];
    fake_eth_advance(UINT64_C(3600000000));
    CHECK(wifi_settle(&f));
    CHECK(fake_wifi_status().calls[WIFI_FAKE_CONNECT] == calls);
    CHECK(strcmp(f.sta.configured.password, wifi_good.password) == 0);
    CHECK(gs232b_esp_wifi_sta_set_enabled(&f.sta, true) == GS232B_OK);
    CHECK(wifi_settle(&f)); wifi_ready(&f);
    CHECK(wifi_snapshot(&f, &s)); const uint64_t epoch = s.session_epoch;
    const wifi_event_sta_disconnected_t stale = {.reason = WIFI_REASON_AUTH_FAIL};
    CHECK(esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &stale, sizeof(stale), 0) == ESP_OK);
    const gs232b_wifi_sta_config_t replacement = {.enabled = true, .ssid = "new network", .password = "newpass1"};
    CHECK(gs232b_esp_wifi_sta_configure(&f.sta, &replacement) == GS232B_OK);
    CHECK(gs232b_esp_wifi_sta_configure(&f.sta, &wifi_good) == GS232B_ERROR_BUSY);
    CHECK(wifi_settle(&f)); CHECK(wifi_snapshot(&f, &s));
    CHECK(s.state == GS232B_WIFI_STA_CONNECTING && s.failure == GS232B_WIFI_STA_FAILURE_NONE);
    CHECK(s.session_epoch > epoch && strcmp(s.ssid, replacement.ssid) == 0);
    CHECK(memcmp(fake_wifi_status().credentials.sta.ssid, replacement.ssid, strlen(replacement.ssid)) == 0);
    CHECK(memcmp(fake_wifi_status().credentials.sta.password, replacement.password, strlen(replacement.password)) == 0);
    gs232b_wifi_sta_config_t invalid = replacement; strcpy(invalid.password, "short");
    CHECK(gs232b_esp_wifi_sta_configure(&f.sta, &invalid) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(strcmp(f.sta.configured.password, replacement.password) == 0);
    CHECK(wifi_close(&f));
    CHECK(wifi_open(&f, NULL)); CHECK(wifi_snapshot(&f, &s));
    CHECK(s.state == GS232B_WIFI_STA_DISABLED && !s.password_set && fake_wifi_status().calls[WIFI_FAKE_INIT] == 0);
    CHECK(gs232b_esp_wifi_sta_set_enabled(&f.sta, true) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(wifi_close(&f));
    return true;
}

int main(void)
{
    if (!validation() || !wrong_password_and_backoff() || !timeout_and_ip_events() ||
        !disable_and_reconfigure() || !wifi_test_faults() || !wifi_test_coexistence() || !wifi_test_ipv4()) { return 1; }
    puts("Wi-Fi STA credentials, bounded retry, lifecycle, fault and Ethernet coexistence tests passed");
    return 0;
}
