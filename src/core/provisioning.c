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

#include "gs232b/provisioning.h"

#include <stdio.h>
#include <string.h>

static bool has_sta_credentials(const gs232b_net_config_t *config)
{
    return config != NULL && config->wifi.enabled && config->wifi.ssid[0] != '\0';
}

static gs232b_time_us_t now(const gs232b_provisioning_t *provisioning)
{
    return provisioning->clock.now_us(provisioning->clock.ctx);
}

static void refresh_remaining(gs232b_provisioning_status_t *status, gs232b_time_us_t current)
{
    if (status->state != GS232B_PROVISIONING_ACTIVE || current < status->started_us ||
        current >= status->deadline_us) {
        status->remaining_us = 0;
    } else {
        status->remaining_us = status->deadline_us - current;
    }
}

static gs232b_error_t start_window(gs232b_provisioning_t *provisioning,
    uint32_t timeout_seconds, bool automatic)
{
    if (timeout_seconds == 0) timeout_seconds = GS232B_PROVISIONING_DEFAULT_WINDOW_SECONDS;
    if (timeout_seconds > GS232B_PROVISIONING_MAX_WINDOW_SECONDS) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const gs232b_time_us_t current = now(provisioning);
    const gs232b_time_us_t duration = (gs232b_time_us_t)timeout_seconds * UINT64_C(1000000);
    if (duration > UINT64_MAX - current) return GS232B_ERROR_RESOURCE_EXHAUSTED;
    gs232b_provisioning_ipv4_t used[4] = {0};
    size_t used_count = 0;
    if (provisioning->active.eth.mode == GS232B_ETHERNET_STATIC) {
        used[used_count++] = (gs232b_provisioning_ipv4_t){
            provisioning->active.eth.ipv4.address, provisioning->active.eth.ipv4.netmask, true};
    }
    if (provisioning->active.wifi.enabled &&
        provisioning->active.wifi.mode == GS232B_WIFI_STA_STATIC) {
        used[used_count++] = (gs232b_provisioning_ipv4_t){
            provisioning->active.wifi.ipv4.address, provisioning->active.wifi.ipv4.netmask, true};
    }
    if (provisioning->status_port.snapshot != NULL) {
        gs232b_ethernet_snapshot_t eth = {0};
        gs232b_wifi_sta_snapshot_t wifi = {0};
        const gs232b_error_t snapshot = provisioning->status_port.snapshot(
            provisioning->status_port.ctx, &eth, &wifi);
        if (snapshot == GS232B_OK) {
            if (eth.ipv4_valid && used_count < sizeof(used) / sizeof(used[0])) {
                used[used_count++] = (gs232b_provisioning_ipv4_t){
                    eth.current.address, eth.current.netmask, true};
            }
            if (wifi.ipv4_valid && used_count < sizeof(used) / sizeof(used[0])) {
                used[used_count++] = (gs232b_provisioning_ipv4_t){
                    wifi.current.address, wifi.current.netmask, true};
            }
        }
    }
    uint32_t address = 0, netmask = 0;
    const gs232b_error_t subnet_error = gs232b_provisioning_select_subnet(
        used, used_count, &address, &netmask);
    if (subnet_error != GS232B_OK) return subnet_error;
    provisioning->status.ap.address = address;
    provisioning->status.ap.gateway = address;
    provisioning->status.ap.netmask = netmask;
    if (provisioning->status.state != GS232B_PROVISIONING_ACTIVE &&
        provisioning->ap_port.start != NULL) {
        const gs232b_error_t error = provisioning->ap_port.start(provisioning->ap_port.ctx,
                                                                  &provisioning->status.ap);
        if (error != GS232B_OK && error != GS232B_ERROR_WOULD_BLOCK) {
            provisioning->status.last_error = error;
            return error;
        }
    }
    provisioning->status.state = GS232B_PROVISIONING_ACTIVE;
    provisioning->status.started_us = current;
    provisioning->status.deadline_us = current + duration;
    provisioning->status.auto_started = automatic;
    provisioning->status.recovery_available = provisioning->status.trial_active;
    provisioning->status.last_error = GS232B_OK;
    refresh_remaining(&provisioning->status, current);
    return GS232B_OK;
}

gs232b_error_t gs232b_provisioning_make_ap_credentials(const uint8_t mac[6],
    gs232b_provisioning_ap_config_t *config)
{
    if (config == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    const uint8_t fallback[6] = {0, 0, 0, 0, 0, 1};
    if (mac == NULL) mac = fallback;
    memset(config, 0, sizeof(*config));
    const int ssid_size = snprintf(config->ssid, sizeof(config->ssid), "GS232B-SETUP-%02X%02X%02X",
                                   mac[3], mac[4], mac[5]);
    const int password_size = snprintf(config->password, sizeof(config->password),
                                       "GS232B-%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
    if (ssid_size <= 0 || password_size <= 0 || (size_t)ssid_size >= sizeof(config->ssid) ||
        (size_t)password_size >= sizeof(config->password)) return GS232B_ERROR_CORRUPT;
    config->address = UINT32_C(0xc0a80401);
    config->netmask = GS232B_PROVISIONING_AP_NETMASK;
    config->gateway = config->address;
    return GS232B_OK;
}

bool gs232b_provisioning_subnet_overlaps(uint32_t address, uint32_t netmask,
    uint32_t ap_address, uint32_t ap_netmask)
{
    if (address == 0 || netmask == 0 || ap_address == 0 || ap_netmask == 0) return false;
    const uint32_t start = address & netmask;
    const uint32_t end = start | ~netmask;
    const uint32_t ap_start = ap_address & ap_netmask;
    const uint32_t ap_end = ap_start | ~ap_netmask;
    return start <= ap_end && ap_start <= end;
}

gs232b_error_t gs232b_provisioning_select_subnet(const gs232b_provisioning_ipv4_t *used,
    size_t used_count, uint32_t *address, uint32_t *netmask)
{
    if ((used_count != 0 && used == NULL) || address == NULL || netmask == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    /* Keep the AP predictable for operators while walking a small private pool. */
    static const uint32_t candidates[] = {
        UINT32_C(0xc0a80401), UINT32_C(0xc0a80501), UINT32_C(0xc0a80601),
        UINT32_C(0xc0a80701), UINT32_C(0xc0a80801), UINT32_C(0x0a4d0101),
        UINT32_C(0xac1f0101)
    };
    for (size_t candidate = 0; candidate < sizeof(candidates) / sizeof(candidates[0]); ++candidate) {
        bool conflict = false;
        for (size_t i = 0; i < used_count; ++i) {
            if (used[i].valid && gs232b_provisioning_subnet_overlaps(used[i].address,
                used[i].netmask, candidates[candidate], GS232B_PROVISIONING_AP_NETMASK)) {
                conflict = true;
                break;
            }
        }
        if (!conflict) {
            *address = candidates[candidate];
            *netmask = GS232B_PROVISIONING_AP_NETMASK;
            return GS232B_OK;
        }
    }
    return GS232B_ERROR_RESOURCE_EXHAUSTED;
}

gs232b_error_t gs232b_provisioning_init(gs232b_provisioning_t *provisioning,
    gs232b_clock_t clock, const gs232b_net_config_t *active, uint64_t revision,
    bool has_persisted_config, gs232b_net_status_port_t status_port,
    gs232b_provisioning_ap_port_t ap_port, const uint8_t mac[6])
{
    (void)revision;
    const gs232b_net_config_t defaults = {0};
    if (provisioning == NULL || clock.now_us == NULL ||
        !gs232b_net_config_valid(active == NULL ? &defaults : active)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(provisioning, 0, sizeof(*provisioning));
    provisioning->clock = clock;
    provisioning->active = active == NULL ? defaults : *active;
    provisioning->status_port = status_port;
    provisioning->ap_port = ap_port;
    if (mac != NULL) memcpy(provisioning->mac, mac, sizeof(provisioning->mac));
    gs232b_error_t error = gs232b_provisioning_make_ap_credentials(provisioning->mac,
                                                                     &provisioning->status.ap);
    if (error != GS232B_OK) return error;
    provisioning->configured = has_persisted_config;
    provisioning->status.first_boot = !has_persisted_config && !has_sta_credentials(&provisioning->active);
    provisioning->status.last_error = GS232B_OK;
    provisioning->initialized = true;
    if (provisioning->status.first_boot) {
        error = start_window(provisioning, GS232B_PROVISIONING_DEFAULT_WINDOW_SECONDS, true);
    }
    return error;
}

gs232b_error_t gs232b_provisioning_start(gs232b_provisioning_t *provisioning,
    uint32_t timeout_seconds)
{
    if (provisioning == NULL || !provisioning->initialized) return GS232B_ERROR_NOT_READY;
    return start_window(provisioning, timeout_seconds, false);
}

gs232b_error_t gs232b_provisioning_stop(gs232b_provisioning_t *provisioning)
{
    if (provisioning == NULL || !provisioning->initialized) return GS232B_ERROR_NOT_READY;
    if (provisioning->status.state == GS232B_PROVISIONING_ACTIVE &&
        provisioning->ap_port.stop != NULL) {
        const gs232b_error_t error = provisioning->ap_port.stop(provisioning->ap_port.ctx);
        if (error != GS232B_OK && error != GS232B_ERROR_WOULD_BLOCK) {
            provisioning->status.last_error = error;
            return error;
        }
    }
    provisioning->status.state = GS232B_PROVISIONING_OFF;
    provisioning->scan.valid = false;
    provisioning->status.auto_started = false;
    provisioning->status.remaining_us = 0;
    provisioning->status.recovery_available = provisioning->status.trial_active;
    provisioning->status.last_error = GS232B_OK;
    return GS232B_OK;
}

gs232b_error_t gs232b_provisioning_tick(gs232b_provisioning_t *provisioning)
{
    if (provisioning == NULL || !provisioning->initialized) return GS232B_ERROR_NOT_READY;
    const gs232b_time_us_t current = now(provisioning);
    if (provisioning->status.state == GS232B_PROVISIONING_ACTIVE &&
        (current < provisioning->status.started_us || current >= provisioning->status.deadline_us)) {
        const gs232b_error_t error = gs232b_provisioning_stop(provisioning);
        if (error != GS232B_OK) return error;
        provisioning->status.state = GS232B_PROVISIONING_EXPIRED;
    }
    refresh_remaining(&provisioning->status, current);
    return GS232B_OK;
}

gs232b_error_t gs232b_provisioning_set_trial_active(gs232b_provisioning_t *provisioning,
    bool active)
{
    if (provisioning == NULL || !provisioning->initialized) return GS232B_ERROR_NOT_READY;
    provisioning->status.trial_active = active;
    provisioning->status.recovery_available = active;
    return GS232B_OK;
}

gs232b_error_t gs232b_provisioning_snapshot(const gs232b_provisioning_t *provisioning,
    gs232b_provisioning_status_t *status)
{
    if (provisioning == NULL || status == NULL || !provisioning->initialized) {
        if (status != NULL) memset(status, 0, sizeof(*status));
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *status = provisioning->status;
    refresh_remaining(status, now(provisioning));
    return GS232B_OK;
}
