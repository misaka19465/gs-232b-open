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

#ifndef TEST_ETHERNET_API_H
#define TEST_ETHERNET_API_H

#include "gs232b/esp_ethernet.h"

typedef enum {
    ETH_FAKE_READ_MAC, ETH_FAKE_FACTORY, ETH_FAKE_INSTALL, ETH_FAKE_SET_MAC,
    ETH_FAKE_NETIF, ETH_FAKE_GLUE, ETH_FAKE_ATTACH, ETH_FAKE_REGISTER,
    ETH_FAKE_DHCP_STOP, ETH_FAKE_SET_IP, ETH_FAKE_SET_DNS, ETH_FAKE_START,
    ETH_FAKE_STOP, ETH_FAKE_FENCE, ETH_FAKE_UNREGISTER, ETH_FAKE_GLUE_DELETE,
    ETH_FAKE_UNINSTALL, ETH_FAKE_GET_IP, ETH_FAKE_GET_DNS, ETH_FAKE_DHCP_START,
    ETH_FAKE_MAC_DELETE, ETH_FAKE_PHY_DELETE, ETH_FAKE_CALLS
} fake_eth_call_t;

typedef struct {
    unsigned calls[ETH_FAKE_CALLS];
    unsigned live_macs, live_phys, live_drivers, live_netifs, live_glues, live_handlers;
    unsigned live_queues, live_semaphores, live_tasks;
    unsigned fail_allocation, allocations;
    bool fail_task;
    uint8_t official_mac[6];
    uint8_t installed_mac[6];
    bool invariant_failed;
    int route_prio;
} fake_eth_status_t;

void fake_eth_reset(void);
fake_eth_status_t fake_eth_status(void);
void fake_eth_fail(fake_eth_call_t call, unsigned after, esp_err_t error);
void fake_eth_flush(void);
void fake_eth_link(gs232b_esp_ethernet_t *adapter, bool up);
void fake_eth_lease(gs232b_esp_ethernet_t *adapter, gs232b_ethernet_ipv4_t ip);
void fake_eth_lost_ip(gs232b_esp_ethernet_t *adapter);
void fake_eth_advance(uint64_t us);
esp_netif_dhcp_status_t fake_eth_dhcp(gs232b_esp_ethernet_t *adapter);
gs232b_ethernet_ipv4_t fake_eth_runtime(gs232b_esp_ethernet_t *adapter);
void fake_eth_fail_allocation(unsigned at, bool task);
bool fake_eth_allocation(void);
void fake_eth_resource(int queues, int semaphores, int tasks);
void fake_eth_pause(fake_eth_call_t call);
bool fake_eth_paused(void);
void fake_eth_release(void);

#endif
