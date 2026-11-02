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

#include "gs232b/wifi_sta.h"
#include "gs232b/ethernet.h"

#include <stddef.h>
#include <string.h>

static bool utf8(const unsigned char *p, size_t size)
{
    for (size_t i = 0; i < size;) {
        const unsigned char first = p[i++];
        if (first < 0x80) { continue; }
        unsigned count;
        uint32_t value, minimum;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; value = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; value = first & 0xf; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; value = first & 7; minimum = 0x10000; }
        else { return false; }
        if (size - i < count) { return false; }
        while (count-- != 0) {
            if ((p[i] & 0xc0) != 0x80) { return false; }
            value = (value << 6) | (p[i++] & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

bool gs232b_wifi_sta_config_valid(const gs232b_wifi_sta_config_t *config)
{
    if (config == NULL || memchr(config->ssid, 0, sizeof(config->ssid)) == NULL ||
        memchr(config->password, 0, sizeof(config->password)) == NULL) { return false; }
    if (config->mode != GS232B_WIFI_STA_DHCP && config->mode != GS232B_WIFI_STA_STATIC) return false;
    if (config->mode == GS232B_WIFI_STA_STATIC) {
        const gs232b_ethernet_config_t ip = {.mode = GS232B_ETHERNET_STATIC,
            .ipv4 = {.address = config->ipv4.address, .netmask = config->ipv4.netmask,
                .gateway = config->ipv4.gateway}};
        if (!gs232b_ethernet_config_valid(&ip)) return false;
    }
    const size_t ssid = strlen(config->ssid), password = strlen(config->password);
    if (ssid == 0 && password == 0) { return !config->enabled; }
    if (ssid == 0 || !utf8((const unsigned char *)config->ssid, ssid)) {
        return false;
    }
    if (password == 0) return true;
    if (password < 8) return false;
    if (password < 64) {
        for (size_t i = 0; i < password; ++i) {
            const unsigned char c = (unsigned char)config->password[i];
            if (c < 0x20 || c > 0x7e) { return false; }
        }
        return true;
    }
    for (size_t i = 0; i < password; ++i) {
        const char c = config->password[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) { return false; }
    }
    return true;
}

gs232b_time_us_t gs232b_wifi_sta_retry_delay(uint32_t consecutive_failures)
{
    if (consecutive_failures == 0) { return 0; }
    if (consecutive_failures >= 6) { return GS232B_WIFI_STA_RETRY_MAX_US; }
    return UINT64_C(1000000) << (consecutive_failures - 1U);
}
