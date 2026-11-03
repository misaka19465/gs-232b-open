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

#include "esp_app_internal.h"

#include <string.h>
#include "esp_mac.h"

static gs232b_error_t status(void *ctx, gs232b_ethernet_snapshot_t *eth,
    gs232b_wifi_sta_snapshot_t *wifi)
{
    gs232b_esp_app_t *runtime = ctx;
    gs232b_error_t first = gs232b_esp_ethernet_snapshot(&runtime->eth, eth);
    gs232b_error_t second = gs232b_esp_wifi_sta_snapshot(&runtime->wifi, wifi);
    if (first != GS232B_OK) *eth = (gs232b_ethernet_snapshot_t){
        .state = GS232B_ETHERNET_ERROR, .last_error = first, .session_epoch = 1};
    if (second != GS232B_OK) *wifi = (gs232b_wifi_sta_snapshot_t){
        .state = GS232B_WIFI_STA_ERROR, .last_error = second, .session_epoch = 1};
    return GS232B_OK;
}

static gs232b_error_t apply(void *ctx, const gs232b_net_config_t *config)
{
    gs232b_esp_app_t *runtime = ctx;
    if (runtime->network_requests == NULL) return GS232B_ERROR_NOT_READY;
    return xQueueOverwrite(runtime->network_requests, config) == pdPASS ? GS232B_OK : GS232B_ERROR_BUSY;
}

static gs232b_error_t ap_start(void *ctx, const gs232b_provisioning_ap_config_t *config)
{
    gs232b_esp_app_t *runtime = ctx;
    if (runtime->ap_requests == NULL) return GS232B_ERROR_NOT_READY;
    const gs232b_app_ap_request_t request = {.enabled = true, .config = *config};
    return xQueueOverwrite(runtime->ap_requests, &request) == pdPASS ? GS232B_OK : GS232B_ERROR_BUSY;
}

static gs232b_error_t ap_stop(void *ctx)
{
    gs232b_esp_app_t *runtime = ctx;
    if (runtime->ap_requests == NULL) return GS232B_ERROR_NOT_READY;
    const gs232b_app_ap_request_t request = {0};
    return xQueueOverwrite(runtime->ap_requests, &request) == pdPASS ? GS232B_OK : GS232B_ERROR_BUSY;
}

static gs232b_error_t ap_scan(void *ctx, gs232b_provisioning_ap_t *aps,
    size_t capacity, size_t *count, bool *truncated)
{
    return gs232b_esp_wifi_sta_ap_scan_async(&((gs232b_esp_app_t *)ctx)->wifi,
        aps, capacity, count, truncated);
}

gs232b_error_t gs232b_app_network_bind(void *ctx, gs232b_app_t *app)
{
    gs232b_esp_app_t *runtime = ctx;
    runtime->network_requests = xQueueCreate(1, sizeof(gs232b_net_config_t));
    runtime->ap_requests = xQueueCreate(1, sizeof(gs232b_app_ap_request_t));
    if (runtime->network_requests == NULL || runtime->ap_requests == NULL) {
        runtime->bind_error = GS232B_ERROR_RESOURCE_EXHAUSTED;
        return runtime->bind_error;
    }
    gs232b_network_config_t saved;
    uint64_t revision = 0;
    gs232b_error_t error = gs232b_persistence_network_load(&app->store, &saved, &revision);
    if (error == GS232B_OK) {
        runtime->desired.eth = saved.eth;
        runtime->desired.wifi = gs232b_persistence_network_wifi(&saved);
    } else if (error != GS232B_ERROR_NOT_FOUND) runtime->bind_error = error;
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
        runtime->bind_error = GS232B_ERROR_IO;
    } else {
        error = gs232b_dispatcher_provisioning_configure(&app->dispatcher,
            (gs232b_provisioning_ap_port_t){runtime, ap_start, ap_stop, ap_scan}, mac);
        if (error != GS232B_OK) runtime->bind_error = error;
    }
    error = gs232b_dispatcher_network_configure(&app->dispatcher, &runtime->desired,
        revision, (gs232b_net_status_port_t){runtime, status});
    if (error != GS232B_OK) runtime->bind_error = error;
    error = gs232b_dispatcher_network_apply_configure(&app->dispatcher,
        (gs232b_net_apply_port_t){runtime, apply});
    if (error != GS232B_OK) runtime->bind_error = error;
    return runtime->bind_error;
}

gs232b_error_t gs232b_app_network_start(void *ctx, gs232b_app_t *app)
{
    gs232b_esp_app_t *runtime = ctx;
    gs232b_ethernet_config_t eth = runtime->desired.eth;
    gs232b_wifi_sta_config_t wifi = runtime->desired.wifi;
    if (eth.mode == GS232B_ETHERNET_DHCP) memset(&eth.ipv4, 0, sizeof(eth.ipv4));
    if (wifi.mode == GS232B_WIFI_STA_DHCP) memset(&wifi.ipv4, 0, sizeof(wifi.ipv4));
    runtime->eth_error = gs232b_esp_ethernet_open_after_reset(&runtime->eth,
        &app->startup_reset, &eth);
    runtime->wifi_error = gs232b_esp_wifi_sta_open(&runtime->wifi, &wifi);
    runtime->ws_error = gs232b_esp_ws_open(&runtime->ws, &app->dispatcher);
    runtime->ap_epoch = 1;
    runtime->applied = runtime->desired;
    return runtime->eth_error != GS232B_OK ? runtime->eth_error :
        runtime->wifi_error != GS232B_OK ? runtime->wifi_error : runtime->ws_error;
}

gs232b_error_t gs232b_app_network_service(void *ctx, gs232b_app_t *app)
{
    gs232b_esp_app_t *runtime = ctx;
    if (runtime->eth.opened) {
        const gs232b_error_t error = gs232b_esp_ethernet_service(&runtime->eth);
        if (error != GS232B_ERROR_WOULD_BLOCK) runtime->eth_error = error;
        /* NOTE: bit4 提交失败立即锁定运动故障，再允许其他恢复/命令提交。 */
        if (!app->outputs.state.output.known) (void)gs232b_motion_tick(&app->motion);
    }
    if (runtime->wifi.opened) {
        const gs232b_error_t error = gs232b_esp_wifi_sta_service(&runtime->wifi);
        if (error != GS232B_ERROR_WOULD_BLOCK) runtime->wifi_error = error;
    }
    gs232b_app_network_requests(runtime);
    if (runtime->ws.opened) {
        if (gs232b_app_network_publish(runtime)) (void)gs232b_esp_ws_service(&runtime->ws);
    }
    return runtime->bind_error != GS232B_OK ? runtime->bind_error :
        runtime->eth_error != GS232B_OK ? runtime->eth_error :
        runtime->wifi_error != GS232B_OK ? runtime->wifi_error : runtime->ws_error;
}
