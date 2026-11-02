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

#ifndef TEST_ESP_ETH_NETIF_GLUE_H
#define TEST_ESP_ETH_NETIF_GLUE_H
#include "esp_eth.h"
typedef struct fake_eth_glue *esp_eth_netif_glue_handle_t;
esp_eth_netif_glue_handle_t esp_eth_new_netif_glue(esp_eth_handle_t);
esp_err_t esp_eth_del_netif_glue(esp_eth_netif_glue_handle_t);
#endif
