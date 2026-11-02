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

void gs232b_sta_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    gs232b_esp_wifi_sta_t *adapter = ctx;
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    if (!adapter->accepting_events || atomic_load(&adapter->cancelled) ||
        adapter->state.session_epoch == UINT64_MAX) { goto done; }
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_CONNECTED && !adapter->state.connected) {
            adapter->state.connected = true;
            adapter->state.state = GS232B_WIFI_STA_DHCP_WAIT;
            adapter->waiting_since_us = (gs232b_time_us_t)esp_timer_get_time();
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
                adapter->waiting_since_us = (gs232b_time_us_t)esp_timer_get_time();
            }
        }
    }
done:
    (void)xSemaphoreGive(adapter->lock);
}
