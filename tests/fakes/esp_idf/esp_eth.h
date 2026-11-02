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

#ifndef TEST_ESP_ETH_H
#define TEST_ESP_ETH_H

#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_event.h"

typedef struct fake_eth_driver *esp_eth_handle_t;
typedef enum { ETH_CMD_S_MAC_ADDR } esp_eth_io_cmd_t;
typedef enum { ETHERNET_EVENT_START, ETHERNET_EVENT_STOP, ETHERNET_EVENT_CONNECTED,
    ETHERNET_EVENT_DISCONNECTED } eth_event_t;
typedef struct { esp_eth_mac_t *mac; esp_eth_phy_t *phy; } esp_eth_config_t;
#define ETH_DEFAULT_CONFIG(mac_, phy_) { .mac = (mac_), .phy = (phy_) }
ESP_EVENT_DECLARE_BASE(ETH_EVENT);

esp_err_t esp_eth_driver_install(const esp_eth_config_t *, esp_eth_handle_t *);
esp_err_t esp_eth_driver_uninstall(esp_eth_handle_t);
esp_err_t esp_eth_start(esp_eth_handle_t);
esp_err_t esp_eth_stop(esp_eth_handle_t);
esp_err_t esp_eth_ioctl(esp_eth_handle_t, esp_eth_io_cmd_t, void *);

#endif
