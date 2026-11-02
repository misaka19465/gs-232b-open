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

#include "esp_wifi_sta_internal.h"

#include <string.h>

void gs232b_sta_invalidate(gs232b_esp_wifi_sta_t *adapter)
{
    if (adapter->state.ipv4_valid) {
        if (adapter->state.session_epoch < UINT64_MAX) { ++adapter->state.session_epoch; }
    }
    adapter->state.ipv4_valid = false;
    memset(&adapter->state.current, 0, sizeof(adapter->state.current));
    if (adapter->state.session_epoch == UINT64_MAX) {
        adapter->accepting_events = false;
        adapter->state.state = GS232B_WIFI_STA_ERROR;
        adapter->state.last_error = GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
}

void gs232b_sta_retry(gs232b_esp_wifi_sta_t *adapter, gs232b_wifi_sta_failure_t failure,
                       gs232b_error_t error, esp_err_t sdk_error, uint16_t reason)
{
    gs232b_sta_invalidate(adapter);
    adapter->state.connected = false;
    adapter->accepting_events = false;
    if (adapter->state.session_epoch == UINT64_MAX) { return; }
    adapter->state.failure = failure;
    adapter->state.last_error = error;
    adapter->state.sdk_error = sdk_error;
    adapter->state.disconnect_reason = reason;
    if (!adapter->state.enabled) { adapter->state.state = GS232B_WIFI_STA_ERROR; return; }
    if (adapter->state.consecutive_failures < UINT32_MAX) { ++adapter->state.consecutive_failures; }
    adapter->state.retry_delay_us = gs232b_wifi_sta_retry_delay(adapter->state.consecutive_failures);
    const int64_t now = esp_timer_get_time();
    const uint64_t start = now >= 0 ? (uint64_t)now : 0;
    adapter->state.retry_at_us = start > UINT64_MAX - adapter->state.retry_delay_us ?
        UINT64_MAX : start + adapter->state.retry_delay_us;
    adapter->state.state = GS232B_WIFI_STA_RETRY_WAIT;
}

static gs232b_wifi_sta_failure_t failure_for(uint16_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return GS232B_WIFI_STA_FAILURE_AUTH;
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return GS232B_WIFI_STA_FAILURE_NO_AP;
    default: return GS232B_WIFI_STA_FAILURE_DISCONNECTED;
    }
}

static void got_ip(gs232b_esp_wifi_sta_t *adapter, const ip_event_got_ip_t *event)
{
    if (!adapter->state.connected) { return; }
    esp_netif_ip_info_t current;
    esp_err_t error = esp_netif_get_ip_info(adapter->netif, &current);
    if (error != ESP_OK) { goto failed; }
    if (current.ip.addr == 0 || memcmp(&current, &event->ip_info, sizeof(current)) != 0) { return; }
    gs232b_wifi_sta_ipv4_t ip = {.address = esp_netif_htonl(current.ip.addr),
        .netmask = esp_netif_htonl(current.netmask.addr), .gateway = esp_netif_htonl(current.gw.addr)};
    for (unsigned i = 0; i < 2; ++i) {
        esp_netif_dns_info_t dns = {0};
        error = esp_netif_get_dns_info(adapter->netif, (esp_netif_dns_type_t)i, &dns);
        if (error != ESP_OK) { goto failed; }
        if (dns.ip.type == ESP_IPADDR_TYPE_V4) { ip.dns[i] = esp_netif_htonl(dns.ip.u_addr.ip4.addr); }
    }
    if (adapter->state.ipv4_valid &&
        (ip.address != adapter->state.current.address || ip.netmask != adapter->state.current.netmask ||
         ip.gateway != adapter->state.current.gateway)) { gs232b_sta_invalidate(adapter); }
    if (adapter->state.session_epoch == UINT64_MAX) { return; }
    adapter->state.current = ip;
    adapter->state.ipv4_valid = true;
    adapter->state.state = GS232B_WIFI_STA_READY;
    adapter->state.failure = GS232B_WIFI_STA_FAILURE_NONE;
    adapter->state.last_error = GS232B_OK;
    adapter->state.sdk_error = 0;
    adapter->state.disconnect_reason = 0;
    adapter->state.consecutive_failures = 0;
    adapter->state.retry_at_us = adapter->state.retry_delay_us = 0;
    return;
failed:
    gs232b_sta_retry(adapter, GS232B_WIFI_STA_FAILURE_DRIVER, gs232b_sta_error(error), error, 0);
}

static void process_event(gs232b_esp_wifi_sta_t *adapter, esp_event_base_t base, int32_t id,
    void *data, gs232b_time_us_t occurred_at_us)
{
    if (!adapter->accepting_events || atomic_load(&adapter->cancelled) ||
        adapter->state.session_epoch == UINT64_MAX) { goto done; }
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_CONNECTED && !adapter->state.connected) {
            adapter->state.connected = true;
            adapter->state.state = GS232B_WIFI_STA_DHCP_WAIT;
            adapter->waiting_since_us = occurred_at_us;
            if (adapter->configured.mode == GS232B_WIFI_STA_STATIC) {
                ip_event_got_ip_t event = {.esp_netif = adapter->netif};
                const esp_err_t error = esp_netif_get_ip_info(adapter->netif, &event.ip_info);
                if (error == ESP_OK) got_ip(adapter, &event);
                else gs232b_sta_retry(adapter, GS232B_WIFI_STA_FAILURE_DRIVER,
                    gs232b_sta_error(error), error, 0);
            }
        } else if (id == WIFI_EVENT_STA_DISCONNECTED && data != NULL) {
            const wifi_event_sta_disconnected_t *event = data;
            gs232b_sta_retry(adapter, failure_for(event->reason), GS232B_ERROR_IO, ESP_OK, event->reason);
        } else if (id == WIFI_EVENT_STA_STOP) {
            gs232b_sta_retry(adapter, GS232B_WIFI_STA_FAILURE_DRIVER, GS232B_ERROR_NOT_READY,
                              ESP_ERR_WIFI_NOT_STARTED, 0);
        }
    } else if (base == IP_EVENT && data != NULL &&
               (id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_STA_LOST_IP)) {
        const ip_event_got_ip_t *event = data;
        if (event->esp_netif != adapter->netif) { goto done; }
        if (id == IP_EVENT_STA_GOT_IP) { got_ip(adapter, event); }
        else if (adapter->state.connected) {
            esp_netif_ip_info_t current;
            const esp_err_t error = esp_netif_get_ip_info(adapter->netif, &current);
            if (error != ESP_OK) {
                gs232b_sta_retry(adapter, GS232B_WIFI_STA_FAILURE_DRIVER, gs232b_sta_error(error), error, 0);
            } else if (current.ip.addr == 0 && adapter->state.ipv4_valid) {
                gs232b_sta_invalidate(adapter);
                if (adapter->state.session_epoch == UINT64_MAX) { goto done; }
                adapter->state.state = GS232B_WIFI_STA_DHCP_WAIT;
                adapter->waiting_since_us = occurred_at_us;
            }
        }
    }
done:
    return;
}

void gs232b_sta_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    gs232b_esp_wifi_sta_t *adapter = ctx;
    gs232b_esp_network_event_t event = {
        .base = base, .id = id, .generation = atomic_load(&adapter->event_generation),
        .occurred_at_us = (uint64_t)esp_timer_get_time()
    };
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && data != NULL) {
        event.reason = ((wifi_event_sta_disconnected_t *)data)->reason;
    } else if (base == IP_EVENT && data != NULL &&
        (id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_STA_LOST_IP)) {
        event.ip = *(ip_event_got_ip_t *)data;
    } else if (base != WIFI_EVENT ||
        (id != WIFI_EVENT_STA_CONNECTED && id != WIFI_EVENT_STA_STOP)) return;
    if (xQueueSend(adapter->events, &event, 0) != pdPASS)
        atomic_store(&adapter->event_lost, true);
}

void gs232b_sta_events_service(gs232b_esp_wifi_sta_t *adapter)
{
    if (xSemaphoreTake(adapter->lock, 0) != pdPASS) return;
    gs232b_esp_network_event_t event;
    for (unsigned count = 0; count < 4U &&
         xQueueReceive(adapter->events, &event, 0) == pdPASS; ++count) {
        if (event.generation != atomic_load(&adapter->event_generation)) continue;
        if (event.base == IP_EVENT)
            process_event(adapter, event.base, event.id, &event.ip, event.occurred_at_us);
        else if (event.id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t disconnected = {.reason = event.reason};
            process_event(adapter, event.base, event.id, &disconnected, event.occurred_at_us);
        } else process_event(adapter, event.base, event.id, NULL, event.occurred_at_us);
    }
    if (atomic_exchange(&adapter->event_lost, false)) {
        gs232b_sta_retry(adapter, GS232B_WIFI_STA_FAILURE_DRIVER,
            GS232B_ERROR_RESOURCE_EXHAUSTED, ESP_ERR_NO_MEM, 0);
    }
    (void)xSemaphoreGive(adapter->lock);
}
