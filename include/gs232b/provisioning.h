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

#ifndef GS232B_PROVISIONING_H
#define GS232B_PROVISIONING_H

#include "gs232b/network_config.h"

#define GS232B_PROVISIONING_DEFAULT_WINDOW_SECONDS 600U
#define GS232B_PROVISIONING_MAX_WINDOW_SECONDS 3600U
#define GS232B_PROVISIONING_AP_SSID_MAX 32U
#define GS232B_PROVISIONING_AP_PASSWORD_MAX 64U
#define GS232B_PROVISIONING_AP_NETMASK UINT32_C(0xffffff00)
#define GS232B_PROVISIONING_SCAN_MAX_APS 64U
#define GS232B_PROVISIONING_SCAN_PAGE_APS 8U
#define GS232B_PROVISIONING_SCAN_TTL_US UINT64_C(30000000)

typedef struct {
    char ssid[GS232B_WIFI_STA_SSID_MAX + 1U];
    int8_t rssi;
    uint8_t auth;
} gs232b_provisioning_ap_t;

typedef struct {
    gs232b_provisioning_ap_t aps[GS232B_PROVISIONING_SCAN_MAX_APS];
    gs232b_time_us_t created_us;
    uint32_t id;
    size_t count;
    bool truncated, valid;
} gs232b_provisioning_scan_t;

typedef struct {
    char ssid[GS232B_PROVISIONING_AP_SSID_MAX + 1U];
    char password[GS232B_PROVISIONING_AP_PASSWORD_MAX + 1U];
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
} gs232b_provisioning_ap_config_t;

typedef struct {
    uint32_t address;
    uint32_t netmask;
    bool valid;
} gs232b_provisioning_ipv4_t;

typedef struct {
    void *ctx;
    /** @brief Start the management AP with the complete generated configuration. */
    gs232b_error_t (*start)(void *ctx, const gs232b_provisioning_ap_config_t *config);
    /** @brief Stop only the management AP; the STA and an active trial remain untouched. */
    gs232b_error_t (*stop)(void *ctx);
    /** @brief 按 RSSI 降序复制最多 capacity 条有效网络；truncated 表示后面还有较弱结果。 */
    gs232b_error_t (*scan)(void *ctx, gs232b_provisioning_ap_t *aps,
        size_t capacity, size_t *count, bool *truncated);
} gs232b_provisioning_ap_port_t;

typedef enum {
    GS232B_PROVISIONING_OFF = 0,
    GS232B_PROVISIONING_ACTIVE,
    GS232B_PROVISIONING_EXPIRED
} gs232b_provisioning_state_t;

typedef struct {
    gs232b_provisioning_state_t state;
    gs232b_provisioning_ap_config_t ap;
    gs232b_time_us_t started_us;
    gs232b_time_us_t deadline_us;
    gs232b_time_us_t remaining_us;
    bool first_boot;
    bool auto_started;
    bool trial_active;
    bool recovery_available;
    gs232b_error_t last_error;
} gs232b_provisioning_status_t;

/* NOTE: The object is a value holder owned by the dispatcher task. No callback may
 * retain the AP config pointer, and all operations are serialized by the caller. */
typedef struct {
    gs232b_clock_t clock;
    gs232b_net_config_t active;
    gs232b_net_status_port_t status_port;
    gs232b_provisioning_ap_port_t ap_port;
    gs232b_provisioning_status_t status;
    gs232b_provisioning_scan_t scan;
    uint8_t mac[6];
    bool configured;
    bool initialized;
} gs232b_provisioning_t;

/**
 * @brief scan_id/cursor 均为零时新建快照；否则分页读取同一次扫描，不重新扫描。
 * @note 每块至多 8 条；快照在 30 秒、AP 停止或新扫描后失效。输出为完整分页 JSON。
 */
gs232b_error_t gs232b_provisioning_scan_page(gs232b_provisioning_t *provisioning,
    uint32_t scan_id, uint32_t cursor, uint8_t *data, size_t capacity, size_t *size);

/** @brief Deterministically derive independent AP credentials from the Wi-Fi MAC. */
gs232b_error_t gs232b_provisioning_make_ap_credentials(const uint8_t mac[6],
    gs232b_provisioning_ap_config_t *config);

/** @brief Return true when an IPv4 network overlaps the proposed AP network. */
bool gs232b_provisioning_subnet_overlaps(uint32_t address, uint32_t netmask,
    uint32_t ap_address, uint32_t ap_netmask);

/** @brief Select a private /24 that does not overlap any currently used interface. */
gs232b_error_t gs232b_provisioning_select_subnet(const gs232b_provisioning_ipv4_t *used,
    size_t used_count, uint32_t *address, uint32_t *netmask);

/**
 * @brief Initialize the buttonless provisioning lifecycle.
 * @param has_persisted_config true only when a valid confirmed network record exists.
 *        A reconnect failure never changes this value and therefore never opens AP.
 */
gs232b_error_t gs232b_provisioning_init(gs232b_provisioning_t *provisioning,
    gs232b_clock_t clock, const gs232b_net_config_t *active, uint64_t revision,
    bool has_persisted_config, gs232b_net_status_port_t status_port,
    gs232b_provisioning_ap_port_t ap_port, const uint8_t mac[6]);

/** @brief Start or extend a bounded management window; zero selects the 10-minute default. */
gs232b_error_t gs232b_provisioning_start(gs232b_provisioning_t *provisioning,
    uint32_t timeout_seconds);
/** @brief Stop the AP only. Active STA trial state is deliberately preserved. */
gs232b_error_t gs232b_provisioning_stop(gs232b_provisioning_t *provisioning);
/** @brief Advance expiry without opening AP after a configured STA failure. */
gs232b_error_t gs232b_provisioning_tick(gs232b_provisioning_t *provisioning);
/** @brief Synchronize the independent network trial with the provisioning manager. */
gs232b_error_t gs232b_provisioning_set_trial_active(gs232b_provisioning_t *provisioning,
    bool active);
/** @brief Copy a redacted-free status for trusted UART formatting. */
gs232b_error_t gs232b_provisioning_snapshot(const gs232b_provisioning_t *provisioning,
    gs232b_provisioning_status_t *status);

#endif
