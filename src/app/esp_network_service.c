#include "esp_app_internal.h"

#include <string.h>

void gs232b_app_network_requests(gs232b_esp_app_t *runtime)
{
    if (runtime->network_requests != NULL)
        (void)xQueueReceive(runtime->network_requests, &runtime->desired, 0);
    const gs232b_net_changes_t changes = gs232b_net_config_changes(&runtime->applied, &runtime->desired);
    if (changes.eth && runtime->eth.opened) {
        gs232b_ethernet_config_t eth = runtime->desired.eth;
        if (eth.mode == GS232B_ETHERNET_DHCP) memset(&eth.ipv4, 0, sizeof(eth.ipv4));
        const gs232b_error_t error = gs232b_esp_ethernet_configure(&runtime->eth, &eth);
        if (error == GS232B_OK) runtime->applied.eth = runtime->desired.eth;
        else if (error != GS232B_ERROR_BUSY && error != GS232B_ERROR_WOULD_BLOCK) runtime->eth_error = error;
    }
    if (changes.wifi && runtime->wifi.opened) {
        gs232b_wifi_sta_config_t wifi = runtime->desired.wifi;
        if (wifi.mode == GS232B_WIFI_STA_DHCP) memset(&wifi.ipv4, 0, sizeof(wifi.ipv4));
        const gs232b_error_t error = gs232b_esp_wifi_sta_configure(&runtime->wifi, &wifi);
        if (error == GS232B_OK) runtime->applied.wifi = runtime->desired.wifi;
        else if (error != GS232B_ERROR_BUSY && error != GS232B_ERROR_WOULD_BLOCK) runtime->wifi_error = error;
    }
    if (runtime->ap_requests != NULL && xQueueReceive(runtime->ap_requests, &runtime->ap, 0) == pdPASS)
        runtime->ap_pending = true;
    if (runtime->ap_pending && runtime->wifi.opened) {
        const gs232b_error_t error = runtime->ap.enabled ?
            gs232b_esp_wifi_sta_ap_start(&runtime->wifi, &runtime->ap.config) :
            gs232b_esp_wifi_sta_ap_stop(&runtime->wifi);
        if (error == GS232B_OK) runtime->ap_pending = false;
        else if (error != GS232B_ERROR_BUSY && error != GS232B_ERROR_WOULD_BLOCK) runtime->wifi_error = error;
    }
}

static bool publish(gs232b_esp_app_t *runtime, gs232b_transport_t transport,
    uint32_t address, uint64_t epoch, esp_netif_t *netif)
{
    char name[GS232B_WS_INTERFACE_NAME_BYTES] = {0};
    if (address != 0 && (netif == NULL || esp_netif_get_netif_impl_name(netif, name) != ESP_OK)) address = 0;
    return gs232b_esp_ws_interface(&runtime->ws, transport, address, epoch == 0 ? 1 : epoch,
        address == 0 ? NULL : name) == GS232B_OK;
}

bool gs232b_app_network_publish(gs232b_esp_app_t *runtime)
{
    gs232b_ethernet_snapshot_t eth;
    gs232b_wifi_sta_snapshot_t wifi;
    if (gs232b_esp_ethernet_snapshot(&runtime->eth, &eth) == GS232B_OK && runtime->eth.job == 0) {
        if (!publish(runtime, GS232B_TRANSPORT_ETHERNET_WS, eth.ipv4_valid ? eth.current.address : 0,
            eth.session_epoch, runtime->eth.netif)) return false;
    } else if (!publish(runtime, GS232B_TRANSPORT_ETHERNET_WS, 0, 1, NULL)) return false;
    const bool wifi_stable = gs232b_esp_wifi_sta_interfaces_stable(&runtime->wifi);
    if (gs232b_esp_wifi_sta_snapshot(&runtime->wifi, &wifi) == GS232B_OK && wifi_stable) {
        if (!publish(runtime, GS232B_TRANSPORT_WIFI_WS, wifi.ipv4_valid ? wifi.current.address : 0,
            wifi.session_epoch, runtime->wifi.netif)) return false;
    } else if (!publish(runtime, GS232B_TRANSPORT_WIFI_WS, 0, 1, NULL)) return false;
    uint32_t address = 0;
    if (wifi_stable && runtime->wifi.ap_started &&
        runtime->app.dispatcher.provisioning.status.state == GS232B_PROVISIONING_ACTIVE)
        address = runtime->wifi.ap_config.address;
    if (address != runtime->ap_address) {
        runtime->ap_address = address;
        if (runtime->ap_epoch != UINT64_MAX) ++runtime->ap_epoch;
    }
    if (runtime->ap_epoch == UINT64_MAX) address = 0;
    return publish(runtime, GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS, address,
        runtime->ap_epoch, address == 0 ? NULL : runtime->wifi.ap_netif);
}
