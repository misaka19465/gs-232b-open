#include "esp_ethernet_internal.h"

#include <string.h>

#include "esp_timer.h"

void gs232b_eth_invalidate(gs232b_esp_ethernet_t *adapter)
{
    if (adapter->state.ipv4_valid) {
        if (adapter->state.session_epoch != UINT64_MAX) {
            ++adapter->state.session_epoch;
        }
        if (adapter->state.session_epoch == UINT64_MAX) {
            adapter->state.last_error = GS232B_ERROR_RESOURCE_EXHAUSTED;
            adapter->state.state = GS232B_ETHERNET_ERROR;
        }
    }
    adapter->state.ipv4_valid = false;
    memset(&adapter->state.current, 0, sizeof(adapter->state.current));
}

void gs232b_eth_fail(gs232b_esp_ethernet_t *adapter, gs232b_error_t error)
{
    gs232b_eth_invalidate(adapter);
    adapter->state.state = GS232B_ETHERNET_ERROR;
    adapter->state.last_error = adapter->state.session_epoch == UINT64_MAX ?
                               GS232B_ERROR_RESOURCE_EXHAUSTED : error;
}

static void wait_for_ip(gs232b_esp_ethernet_t *adapter)
{
    if (adapter->state.session_epoch == UINT64_MAX) { return; }
    const int64_t now = esp_timer_get_time();
    adapter->waiting_since_us = now < 0 ? 0 : (uint64_t)now;
    adapter->state.state = GS232B_ETHERNET_LINK_UP_NO_IP;
    if (adapter->state.configured.mode == GS232B_ETHERNET_DHCP) {
        esp_netif_dhcp_status_t status;
        const esp_err_t error = esp_netif_dhcpc_get_status(adapter->netif, &status);
        if (error != ESP_OK) { gs232b_eth_fail(adapter, gs232b_eth_error(error)); return; }
        adapter->state.dhcp_running = status == ESP_NETIF_DHCP_STARTED;
        adapter->state.state = adapter->state.dhcp_running ? GS232B_ETHERNET_DHCP_WAIT :
                                                          GS232B_ETHERNET_LINK_UP_NO_IP;
    }
}

static bool same_address(const gs232b_ethernet_ipv4_t *a, const gs232b_ethernet_ipv4_t *b)
{
    return a->address == b->address && a->netmask == b->netmask && a->gateway == b->gateway;
}

static void got_ip(gs232b_esp_ethernet_t *adapter, const ip_event_got_ip_t *event)
{
    if (!adapter->state.link_up || event->ip_info.ip.addr == 0 ||
        adapter->state.session_epoch == UINT64_MAX) { return; }
    /* NOTE: 排除已排队的旧地址事件；重建 netif 及事件屏障隔离旧驱动代次。 */
    esp_netif_ip_info_t current;
    esp_err_t error = esp_netif_get_ip_info(adapter->netif, &current);
    if (error != ESP_OK) { gs232b_eth_fail(adapter, gs232b_eth_error(error)); return; }
    if (current.ip.addr != event->ip_info.ip.addr ||
        current.netmask.addr != event->ip_info.netmask.addr ||
        current.gw.addr != event->ip_info.gw.addr) { return; }
    gs232b_ethernet_ipv4_t ip = {
        .address = esp_netif_htonl(current.ip.addr),
        .netmask = esp_netif_htonl(current.netmask.addr),
        .gateway = esp_netif_htonl(current.gw.addr)
    };
    for (unsigned i = 0; i < 2; ++i) {
        esp_netif_dns_info_t dns = {0};
        error = esp_netif_get_dns_info(adapter->netif, (esp_netif_dns_type_t)i, &dns);
        if (error != ESP_OK) { gs232b_eth_fail(adapter, gs232b_eth_error(error)); return; }
        if (dns.ip.type == ESP_IPADDR_TYPE_V4) {
            ip.dns[i] = esp_netif_htonl(dns.ip.u_addr.ip4.addr);
        }
    }
    if (adapter->state.configured.mode == GS232B_ETHERNET_STATIC &&
        !same_address(&ip, &adapter->state.configured.ipv4)) { return; }
    if (adapter->state.ipv4_valid && !same_address(&ip, &adapter->state.current)) {
        gs232b_eth_invalidate(adapter);
    }
    if (adapter->state.session_epoch == UINT64_MAX) { return; }
    adapter->state.current = ip;
    adapter->state.ipv4_valid = true;
    adapter->state.state = GS232B_ETHERNET_READY;
    adapter->state.last_error = GS232B_OK;
}

void gs232b_eth_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    gs232b_esp_ethernet_t *adapter = ctx;
    if (data == NULL) { return; }
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    if (!adapter->accepting_events || atomic_load(&adapter->cancelled) ||
        adapter->state.session_epoch == UINT64_MAX) { goto done; }
    if (base == ETH_EVENT) {
        if (*(esp_eth_handle_t *)data != adapter->driver) { goto done; }
        switch (id) {
        case ETHERNET_EVENT_CONNECTED:
            if (!adapter->state.link_up) {
                adapter->state.link_up = true;
                adapter->state.last_error = GS232B_OK;
                wait_for_ip(adapter);
            }
            if (adapter->state.configured.mode == GS232B_ETHERNET_STATIC) {
                /* NOTE: 无 DHCP_CLIENT 标志时，glue 不发布静态 GOT_IP；link up
                 * 已完成 netif_up，可据实际地址确认静态接口可用。 */
                ip_event_got_ip_t event = {.esp_netif = adapter->netif};
                const esp_err_t error = esp_netif_get_ip_info(adapter->netif, &event.ip_info);
                if (error == ESP_OK) { got_ip(adapter, &event); }
                else { gs232b_eth_fail(adapter, gs232b_eth_error(error)); }
            }
            break;
        case ETHERNET_EVENT_DISCONNECTED:
        case ETHERNET_EVENT_STOP:
            gs232b_eth_invalidate(adapter);
            if (adapter->state.session_epoch == UINT64_MAX) { goto done; }
            adapter->state.link_up = false;
            adapter->state.state = GS232B_ETHERNET_LINK_DOWN;
            adapter->state.last_error = GS232B_OK;
            esp_netif_dhcp_status_t status;
            adapter->state.dhcp_running =
                esp_netif_dhcpc_get_status(adapter->netif, &status) == ESP_OK &&
                status == ESP_NETIF_DHCP_STARTED;
            break;
        default: break;
        }
    } else if (base == IP_EVENT &&
               (id == IP_EVENT_ETH_GOT_IP || id == IP_EVENT_ETH_LOST_IP)) {
        const ip_event_got_ip_t *event = data;
        if (event->esp_netif != adapter->netif) { goto done; }
        if (id == IP_EVENT_ETH_GOT_IP) { got_ip(adapter, event); }
        else {
            /* NOTE: LOST_IP 可由延迟定时器产生，已获得新租约时忽略旧通知。 */
            esp_netif_ip_info_t current;
            const esp_err_t error = esp_netif_get_ip_info(adapter->netif, &current);
            if (error != ESP_OK) { gs232b_eth_fail(adapter, gs232b_eth_error(error)); }
            else if (current.ip.addr == 0) {
                gs232b_eth_invalidate(adapter);
                if (adapter->state.link_up) { wait_for_ip(adapter); }
            }
        }
    }
done:
    (void)xSemaphoreGive(adapter->lock);
}
