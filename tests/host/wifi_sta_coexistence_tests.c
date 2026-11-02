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
#include "gs232b/spi_bus.h"

bool wifi_test_coexistence(void)
{
    wifi_fixture_t f; CHECK(wifi_open(&f, &wifi_good));
    gs232b_fake_spi_bus_t spi;
    gs232b_shared_spi_t bus;
    gs232b_output_manager_t outputs;
    gs232b_esp_ethernet_t eth = {0};
    CHECK(gs232b_fake_spi_bus_init(&spi, &bus));
    CHECK(gs232b_output_manager_init(&outputs,
        (gs232b_clock_t){&spi, gs232b_fake_spi_bus_now}, gs232b_shared_spi_output_driver(&bus), 100) == GS232B_OK);
    CHECK(gs232b_esp_ethernet_open(&eth, &bus, &outputs, NULL) == GS232B_OK);
    CHECK(gs232b_esp_ethernet_recover(&eth) == GS232B_OK);
    uint64_t deadline = wifi_wall_ms() + 3000;
    while (eth.reset.phase != GS232B_W5500_READY && wifi_wall_ms() < deadline) {
        fake_eth_flush();
        const gs232b_error_t serviced = gs232b_esp_ethernet_service(&eth);
        CHECK(serviced == GS232B_OK || serviced == GS232B_ERROR_WOULD_BLOCK);
        atomic_fetch_add(&spi.now_us, UINT64_C(10000));
        sched_yield();
    }
    CHECK(eth.reset.phase == GS232B_W5500_READY);
    fake_eth_link(&eth, true);
    const gs232b_ethernet_ipv4_t eth_ip = {.address = 0xc0a80133, .netmask = 0xffffff00, .gateway = 0xc0a80101};
    fake_eth_lease(&eth, eth_ip);
    gs232b_ethernet_snapshot_t before, after;
    (void)gs232b_esp_ethernet_service(&eth);
    CHECK(gs232b_esp_ethernet_snapshot(&eth, &before) == GS232B_OK);
    CHECK(before.state == GS232B_ETHERNET_READY);
    wifi_ready(&f);
    CHECK(f.sta.netif != eth.netif && fake_eth_status().live_netifs == 2);
    gs232b_wifi_sta_snapshot_t sta; CHECK(wifi_snapshot(&f, &sta));
    CHECK(memcmp(sta.mac, before.mac, 6) != 0 &&
        fake_eth_status().route_prio == GS232B_ESP_ETHERNET_ROUTE_PRIORITY &&
        GS232B_ESP_ETHERNET_ROUTE_PRIORITY > GS232B_ESP_WIFI_STA_ROUTE_PRIORITY);
    const unsigned stops = fake_eth_status().calls[ETH_FAKE_STOP];
    esp_netif_t *const eth_netif = eth.netif;
    for (unsigned i = 0; i < 3; ++i) {
        fake_wifi_disconnected(WIFI_REASON_AUTH_FAIL);
        CHECK(wifi_snapshot(&f, &sta));
        fake_eth_advance(sta.retry_delay_us);
        CHECK(wifi_settle(&f)); wifi_ready(&f);
        CHECK(gs232b_esp_wifi_sta_set_enabled(&f.sta, false) == GS232B_OK);
        CHECK(wifi_settle(&f));
        (void)gs232b_esp_ethernet_service(&eth);
        CHECK(gs232b_esp_ethernet_snapshot(&eth, &after) == GS232B_OK);
        CHECK(after.state == GS232B_ETHERNET_READY && after.session_epoch == before.session_epoch);
        CHECK(after.ipv4_valid && after.current.address == eth_ip.address && eth.netif == eth_netif);
        CHECK(fake_eth_status().calls[ETH_FAKE_STOP] == stops && atomic_load(&bus.ethernet_enabled));
        CHECK(gs232b_esp_wifi_sta_set_enabled(&f.sta, true) == GS232B_OK);
        CHECK(wifi_settle(&f)); wifi_ready(&f);
    }
    CHECK(wifi_snapshot(&f, &sta) && sta.state == GS232B_WIFI_STA_READY);
    const uint64_t sta_epoch = sta.session_epoch;
    fake_eth_link(&eth, false);
    (void)gs232b_esp_ethernet_service(&eth);
    CHECK(gs232b_esp_ethernet_snapshot(&eth, &after) == GS232B_OK);
    CHECK(after.state == GS232B_ETHERNET_LINK_DOWN && !after.ipv4_valid);
    CHECK(wifi_snapshot(&f, &sta) && sta.state == GS232B_WIFI_STA_READY &&
        sta.session_epoch == sta_epoch);
    fake_eth_link(&eth, true);
    fake_eth_lease(&eth, eth_ip);
    (void)gs232b_esp_ethernet_service(&eth);
    CHECK(gs232b_esp_ethernet_snapshot(&eth, &after) == GS232B_OK);
    CHECK(after.state == GS232B_ETHERNET_READY && after.current.address == eth_ip.address);
    const uint64_t eth_epoch_after_failover = after.session_epoch;
    CHECK(wifi_snapshot(&f, &sta) && sta.state == GS232B_WIFI_STA_READY &&
        sta.session_epoch == sta_epoch);
    CHECK(gs232b_esp_wifi_sta_set_enabled(&f.sta, false) == GS232B_OK);
    CHECK(wifi_settle(&f));
    /* NOTE: STA 关闭后仍使用同一个 Ethernet netif 和默认事件循环更新租约。 */
    CHECK(wifi_close_result(&f) == GS232B_OK);
    fake_eth_lease(&eth, eth_ip);
    (void)gs232b_esp_ethernet_service(&eth);
    CHECK(gs232b_esp_ethernet_snapshot(&eth, &after) == GS232B_OK);
    CHECK(after.state == GS232B_ETHERNET_READY && after.session_epoch == eth_epoch_after_failover);
    CHECK(eth.netif == eth_netif && fake_eth_status().calls[ETH_FAKE_STOP] == stops);
    deadline = wifi_wall_ms() + 3000;
    gs232b_error_t result = GS232B_ERROR_WOULD_BLOCK;
    while (result == GS232B_ERROR_WOULD_BLOCK && wifi_wall_ms() < deadline) {
        fake_eth_flush(); result = gs232b_esp_ethernet_close(&eth); sched_yield();
    }
    CHECK(result == GS232B_OK);
    while (fake_eth_status().live_tasks != 0 && wifi_wall_ms() < deadline) { sched_yield(); }
    CHECK(fake_eth_status().live_tasks == 0 && fake_eth_status().live_netifs == 0 && fake_eth_status().live_handlers == 0);
    CHECK(fake_eth_status().live_queues == 0 && fake_eth_status().live_semaphores == 0);
    gs232b_fake_spi_bus_destroy(&spi);
    return true;
}
