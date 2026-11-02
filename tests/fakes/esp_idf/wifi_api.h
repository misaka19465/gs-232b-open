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

#ifndef TEST_WIFI_API_H
#define TEST_WIFI_API_H
#include "gs232b/esp_wifi_sta.h"
#include "esp_wifi.h"
typedef enum { WIFI_FAKE_ATTACH, WIFI_FAKE_DEFAULTS, WIFI_FAKE_INIT, WIFI_FAKE_STORAGE,
    WIFI_FAKE_MODE, WIFI_FAKE_CONFIG, WIFI_FAKE_MAC, WIFI_FAKE_START, WIFI_FAKE_CONNECT,
    WIFI_FAKE_STOP, WIFI_FAKE_DEINIT, WIFI_FAKE_CLEAR, WIFI_FAKE_SCAN_START,
    WIFI_FAKE_SCAN_COUNT, WIFI_FAKE_SCAN_RECORD, WIFI_FAKE_SCAN_CLEAR,
    WIFI_FAKE_CALLS } fake_wifi_call_t;
typedef struct {
    unsigned calls[WIFI_FAKE_CALLS];
    bool initialized, started;
    wifi_mode_t mode;
    wifi_config_t credentials;
} fake_wifi_status_t;
void fake_wifi_reset(void);
void fake_wifi_scan_results(const wifi_ap_record_t *records, uint16_t count);
void fake_wifi_scan_pause(bool pause);
bool fake_wifi_scan_paused(void);
fake_wifi_status_t fake_wifi_status(void);
void fake_wifi_fail(fake_wifi_call_t which, esp_err_t error);
void fake_wifi_connected(void);
void fake_wifi_disconnected(uint8_t reason);
void fake_wifi_lease(gs232b_esp_wifi_sta_t *adapter, gs232b_wifi_sta_ipv4_t ip);
void fake_wifi_lost_ip(gs232b_esp_wifi_sta_t *adapter);
#endif
