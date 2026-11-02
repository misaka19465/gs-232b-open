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

#include "wifi_sta_test_support.h"

static bool allocation_and_ownership(void)
{
    for (unsigned i = 1; i <= 5; ++i) {
        wifi_fixture_t f = {0}; fake_eth_reset(); fake_wifi_reset();
        fake_eth_fail_allocation(i, false);
        CHECK(gs232b_esp_wifi_sta_open(&f.sta, &wifi_good) == GS232B_ERROR_RESOURCE_EXHAUSTED);
        CHECK(fake_eth_status().live_handlers == 0 && fake_eth_status().live_semaphores == 0 &&
              fake_eth_status().live_queues == 0);
    }
    wifi_fixture_t f = {0}; fake_eth_reset(); fake_wifi_reset();
    fake_eth_fail_allocation(0, true);
    CHECK(gs232b_esp_wifi_sta_open(&f.sta, &wifi_good) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(fake_eth_status().live_handlers == 0);
    fake_eth_reset(); fake_wifi_reset();
    fake_eth_fail_allocation(0, true);
    fake_eth_fail(ETH_FAKE_UNREGISTER, 0, ESP_FAIL);
    CHECK(gs232b_esp_wifi_sta_open(&f.sta, &wifi_good) == GS232B_ERROR_IO);
    CHECK(f.sta.opened && f.sta.closing == 4);
    CHECK(wifi_close(&f));
    fake_eth_reset(); fake_wifi_reset();
    fake_eth_fail(ETH_FAKE_REGISTER, 0, ESP_FAIL);
    CHECK(gs232b_esp_wifi_sta_open(&f.sta, &wifi_good) == GS232B_ERROR_IO);
    CHECK(fake_eth_status().live_handlers == 0 && fake_eth_status().live_semaphores == 0);
    CHECK(wifi_open(&f, &wifi_good));
    gs232b_esp_wifi_sta_t second = {0};
    CHECK(gs232b_esp_wifi_sta_open(&second, &wifi_good) == GS232B_ERROR_BUSY);
    CHECK(gs232b_esp_wifi_sta_open(&f.sta, &wifi_good) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(xSemaphoreTake(f.sta.lock, 0) == pdPASS);
    gs232b_wifi_sta_snapshot_t s; memset(&s, 0x5a, sizeof(s));
    CHECK(gs232b_esp_wifi_sta_snapshot(&f.sta, &s) == GS232B_ERROR_WOULD_BLOCK);
    const gs232b_wifi_sta_snapshot_t empty = {0}; CHECK(memcmp(&s, &empty, sizeof(s)) == 0);
    CHECK(xSemaphoreGive(f.sta.lock) == pdPASS);
    CHECK(wifi_close(&f));
    return true;
}

static bool event_errors_and_epoch(void)
{
    const fake_eth_call_t phases[] = {ETH_FAKE_GET_IP, ETH_FAKE_GET_DNS};
    for (unsigned i = 0; i < sizeof(phases) / sizeof(phases[0]); ++i) {
        wifi_fixture_t f; CHECK(wifi_open(&f, &wifi_good));
        fake_eth_fail(phases[i], 0, ESP_FAIL);
        wifi_ready(&f);
        gs232b_wifi_sta_snapshot_t s; CHECK(wifi_snapshot(&f, &s));
        CHECK(s.state == GS232B_WIFI_STA_RETRY_WAIT && s.failure == GS232B_WIFI_STA_FAILURE_DRIVER);
        CHECK(s.sdk_error == ESP_FAIL && !s.ipv4_valid);
        CHECK(wifi_close(&f));
    }
    wifi_fixture_t f; CHECK(wifi_open(&f, &wifi_good)); wifi_ready(&f);
    CHECK(xSemaphoreTake(f.sta.lock, 0) == pdPASS);
    f.sta.state.session_epoch = UINT64_MAX - 1;
    CHECK(xSemaphoreGive(f.sta.lock) == pdPASS);
    CHECK(gs232b_esp_wifi_sta_configure(&f.sta, &wifi_good) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    gs232b_wifi_sta_snapshot_t s; CHECK(wifi_snapshot(&f, &s));
    CHECK(s.state == GS232B_WIFI_STA_ERROR && s.session_epoch == UINT64_MAX && !s.ipv4_valid);
    CHECK(s.last_error == GS232B_ERROR_RESOURCE_EXHAUSTED);
    const unsigned connects = fake_wifi_status().calls[WIFI_FAKE_CONNECT];
    fake_eth_advance(UINT64_C(300000000));
    fake_wifi_disconnected(WIFI_REASON_AUTH_FAIL);
    CHECK(gs232b_esp_wifi_sta_service(&f.sta) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(fake_wifi_status().calls[WIFI_FAKE_CONNECT] == connects);
    CHECK(wifi_close(&f));
    return true;
}

static bool sdk_failures(void)
{
    const fake_wifi_call_t phases[] = {WIFI_FAKE_ATTACH, WIFI_FAKE_DEFAULTS, WIFI_FAKE_INIT,
        WIFI_FAKE_STORAGE, WIFI_FAKE_MODE, WIFI_FAKE_CONFIG, WIFI_FAKE_MAC, WIFI_FAKE_START, WIFI_FAKE_CONNECT};
    for (unsigned i = 0; i < sizeof(phases) / sizeof(phases[0]); ++i) {
        wifi_fixture_t f = {0}; fake_eth_reset(); fake_wifi_reset();
        fake_wifi_fail(phases[i], ESP_FAIL);
        CHECK(gs232b_esp_wifi_sta_open(&f.sta, &wifi_good) == GS232B_OK);
        CHECK(wifi_settle(&f));
        gs232b_wifi_sta_snapshot_t s; CHECK(wifi_snapshot(&f, &s));
        CHECK(s.state == GS232B_WIFI_STA_RETRY_WAIT && s.failure == GS232B_WIFI_STA_FAILURE_DRIVER);
        CHECK(s.sdk_error == ESP_FAIL && s.last_error == GS232B_ERROR_IO && s.consecutive_failures == 1);
        fake_eth_advance(s.retry_delay_us); CHECK(wifi_settle(&f));
        CHECK(wifi_snapshot(&f, &s)); CHECK(s.state == GS232B_WIFI_STA_CONNECTING);
        CHECK(wifi_close(&f));
    }
    const fake_eth_call_t netif_phases[] = {ETH_FAKE_NETIF, ETH_FAKE_FENCE};
    for (unsigned i = 0; i < sizeof(netif_phases) / sizeof(netif_phases[0]); ++i) {
        wifi_fixture_t f = {0}; fake_eth_reset(); fake_wifi_reset();
        fake_eth_fail(netif_phases[i], 0, ESP_ERR_NO_MEM);
        CHECK(gs232b_esp_wifi_sta_open(&f.sta, &wifi_good) == GS232B_OK);
        CHECK(wifi_settle(&f));
        gs232b_wifi_sta_snapshot_t s; CHECK(wifi_snapshot(&f, &s));
        CHECK(s.state == GS232B_WIFI_STA_RETRY_WAIT && s.sdk_error == ESP_ERR_NO_MEM);
        CHECK(wifi_close(&f));
    }
    return true;
}

static bool cleanup_retry(void)
{
    const fake_wifi_call_t phases[] = {WIFI_FAKE_STOP, WIFI_FAKE_DEINIT, WIFI_FAKE_CLEAR};
    for (unsigned i = 0; i < sizeof(phases) / sizeof(phases[0]); ++i) {
        wifi_fixture_t f; CHECK(wifi_open(&f, &wifi_good)); wifi_ready(&f);
        fake_wifi_fail(phases[i], ESP_FAIL);
        CHECK(wifi_close_result(&f) == GS232B_ERROR_IO && f.sta.opened);
        CHECK(wifi_close(&f));
    }
    wifi_fixture_t f; CHECK(wifi_open(&f, &wifi_good));
    fake_eth_fail(ETH_FAKE_UNREGISTER, 0, ESP_FAIL);
    CHECK(wifi_close_result(&f) == GS232B_ERROR_IO);
    CHECK(wifi_close(&f));
    CHECK(wifi_open(&f, &wifi_good));
    fake_eth_pause(ETH_FAKE_FENCE);
    CHECK(gs232b_esp_wifi_sta_configure(&f.sta, &wifi_good) == GS232B_OK);
    const uint64_t deadline = wifi_wall_ms() + 3000;
    while (!fake_eth_paused() && wifi_wall_ms() < deadline) { sched_yield(); }
    CHECK(fake_eth_paused());
    CHECK(gs232b_esp_wifi_sta_close(&f.sta) == GS232B_ERROR_WOULD_BLOCK);
    const unsigned calls = fake_wifi_status().calls[WIFI_FAKE_CONNECT];
    fake_eth_release(); CHECK(wifi_close(&f));
    CHECK(fake_wifi_status().calls[WIFI_FAKE_CONNECT] == calls);
    return true;
}

bool wifi_test_faults(void)
{
    return allocation_and_ownership() && sdk_failures() && event_errors_and_epoch() && cleanup_retry();
}
