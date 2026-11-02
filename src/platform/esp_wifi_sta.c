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

#include <stdio.h>
#include <string.h>

static atomic_flag radio_owned = ATOMIC_FLAG_INIT;

gs232b_error_t gs232b_sta_error(esp_err_t error)
{
    switch (error) {
    case ESP_OK: return GS232B_OK;
    case ESP_ERR_INVALID_ARG: return GS232B_ERROR_INVALID_ARGUMENT;
    case ESP_ERR_INVALID_STATE: return GS232B_ERROR_NOT_READY;
    case ESP_ERR_NO_MEM: return GS232B_ERROR_RESOURCE_EXHAUSTED;
    case ESP_ERR_TIMEOUT: return GS232B_ERROR_TIMEOUT;
    default: return GS232B_ERROR_IO;
    }
}

static void worker(void *ctx)
{
    gs232b_esp_wifi_sta_t *adapter = ctx;
    unsigned job;
    while (xQueueReceive(adapter->requests, &job, portMAX_DELAY) == pdPASS) {
        if (job == GS232B_STA_JOB_EXIT) { break; }
        if (job == GS232B_STA_JOB_SCAN) {
            const gs232b_error_t result = gs232b_esp_wifi_sta_ap_scan(adapter,
                adapter->scan_aps, GS232B_PROVISIONING_SCAN_MAX_APS,
                &adapter->scan_count, &adapter->scan_truncated);
            (void)xQueueSend(adapter->results, &result, portMAX_DELAY);
            continue;
        }
        gs232b_error_t result = gs232b_sta_stop(adapter);
        if (result == GS232B_OK && job != GS232B_STA_JOB_STOP &&
            job != GS232B_STA_JOB_EXIT &&
            (adapter->configured.enabled || adapter->ap_requested) &&
            !atomic_load(&adapter->cancelled)) {
            result = gs232b_sta_start(adapter);
        }
        (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
        if (result != GS232B_OK) {
            if (!atomic_load(&adapter->cancelled)) {
                gs232b_sta_retry(adapter, GS232B_WIFI_STA_FAILURE_DRIVER, result,
                                  adapter->state.sdk_error, 0);
            } else {
                adapter->state.state = GS232B_WIFI_STA_ERROR;
                adapter->state.failure = GS232B_WIFI_STA_FAILURE_DRIVER;
                adapter->state.last_error = result;
            }
        } else if (!adapter->configured.enabled) {
            adapter->state.state = GS232B_WIFI_STA_DISABLED;
        }
        (void)xSemaphoreGive(adapter->lock);
        (void)xQueueSend(adapter->results, &result, portMAX_DELAY);
    }
    (void)xSemaphoreGive(adapter->stopped);
    vTaskDelete(NULL);
}

static gs232b_error_t dispatch(gs232b_esp_wifi_sta_t *adapter, unsigned job)
{
    if (xQueueSend(adapter->requests, &job, 0) != pdPASS) { return GS232B_ERROR_BUSY; }
    adapter->job = job;
    return GS232B_OK;
}

static gs232b_error_t release(gs232b_esp_wifi_sta_t *adapter)
{
    if (adapter->fence_handler != NULL) {
        const esp_err_t error = esp_event_handler_instance_unregister(
            GS232B_STA_FENCE_EVENT, 0, adapter->fence_handler);
        if (error != ESP_OK) { return gs232b_sta_error(error); }
    }
    if (adapter->requests != NULL) { vQueueDelete(adapter->requests); }
    if (adapter->results != NULL) { vQueueDelete(adapter->results); }
    if (adapter->fence != NULL) { vQueueDelete(adapter->fence); }
    if (adapter->events != NULL) { vQueueDelete(adapter->events); }
    if (adapter->stopped != NULL) { vSemaphoreDelete(adapter->stopped); }
    if (adapter->lock != NULL) { vSemaphoreDelete(adapter->lock); }
    memset(adapter, 0, sizeof(*adapter));
    atomic_flag_clear(&radio_owned);
    return GS232B_OK;
}

gs232b_error_t gs232b_esp_wifi_sta_open(gs232b_esp_wifi_sta_t *adapter,
                                      const gs232b_wifi_sta_config_t *config)
{
    const gs232b_wifi_sta_config_t disabled = {0};
    if (config == NULL) { config = &disabled; }
    if (adapter == NULL || adapter->opened || adapter->lock != NULL ||
        !gs232b_wifi_sta_config_valid(config)) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (atomic_flag_test_and_set(&radio_owned)) { return GS232B_ERROR_BUSY; }
    esp_err_t error = esp_netif_init();
    if (error == ESP_OK) {
        error = esp_event_loop_create_default();
        if (error == ESP_ERR_INVALID_STATE) { error = ESP_OK; }
    }
    if (error != ESP_OK) {
        atomic_flag_clear(&radio_owned);
        return gs232b_sta_error(error);
    }
    adapter->lock = xSemaphoreCreateMutex();
    adapter->fence = xQueueCreate(1, sizeof(uint64_t));
    adapter->events = xQueueCreate(GS232B_NETWORK_EVENT_CAPACITY, sizeof(gs232b_esp_network_event_t));
    atomic_init(&adapter->event_generation, 1);
    atomic_init(&adapter->event_lost, false);
    adapter->stopped = xSemaphoreCreateBinary();
    adapter->requests = xQueueCreate(1, sizeof(unsigned));
    adapter->results = xQueueCreate(1, sizeof(gs232b_error_t));
    if (adapter->lock == NULL || adapter->fence == NULL || adapter->stopped == NULL ||
        adapter->requests == NULL || adapter->results == NULL || adapter->events == NULL) {
        (void)release(adapter);
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    error = esp_event_handler_instance_register(GS232B_STA_FENCE_EVENT, 0,
        gs232b_sta_fence_event, adapter, &adapter->fence_handler);
    if (error != ESP_OK) { (void)release(adapter); return gs232b_sta_error(error); }
    if (xTaskCreate(worker, "gs_sta", 4096, adapter, 3, &adapter->worker) != pdPASS) {
        /* NOTE: unregister 失败仍保留实例及 radio 所有权，调用 close 重试清理。 */
        adapter->opened = true;
        adapter->closing = 4;
        const gs232b_error_t cleaned = release(adapter);
        return cleaned == GS232B_OK ? GS232B_ERROR_RESOURCE_EXHAUSTED : cleaned;
    }
    adapter->opened = true;
    adapter->state.session_epoch = 1;
    return gs232b_esp_wifi_sta_configure(adapter, config);
}

gs232b_error_t gs232b_esp_wifi_sta_configure(gs232b_esp_wifi_sta_t *adapter,
                                           const gs232b_wifi_sta_config_t *config)
{
    if (adapter == NULL || !gs232b_wifi_sta_config_valid(config)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!adapter->opened || adapter->closing != 0) { return GS232B_ERROR_NOT_READY; }
    if (adapter->job != GS232B_STA_JOB_NONE) { return GS232B_ERROR_BUSY; }
    if (xSemaphoreTake(adapter->lock, 0) != pdPASS) { return GS232B_ERROR_WOULD_BLOCK; }
    if (adapter->state.session_epoch == UINT64_MAX) {
        (void)xSemaphoreGive(adapter->lock);
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    adapter->accepting_events = false;
    gs232b_sta_invalidate(adapter);
    if (adapter->state.session_epoch == UINT64_MAX) {
        adapter->state.connected = false;
        (void)xSemaphoreGive(adapter->lock);
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    adapter->configured = *config;
    adapter->state.enabled = config->enabled;
    adapter->state.password_set = config->password[0] != '\0';
    memcpy(adapter->state.ssid, config->ssid, sizeof(adapter->state.ssid));
    adapter->state.connected = false;
    adapter->state.failure = GS232B_WIFI_STA_FAILURE_NONE;
    adapter->state.last_error = GS232B_OK;
    adapter->state.sdk_error = 0;
    adapter->state.disconnect_reason = 0;
    adapter->state.consecutive_failures = 0;
    adapter->state.retry_at_us = adapter->state.retry_delay_us = 0;
    adapter->state.state = GS232B_WIFI_STA_STARTING;
    atomic_store(&adapter->cancelled, false);
    (void)xSemaphoreGive(adapter->lock);
    return dispatch(adapter, GS232B_STA_JOB_APPLY);
}

gs232b_error_t gs232b_esp_wifi_sta_set_enabled(gs232b_esp_wifi_sta_t *adapter, bool enabled)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened || adapter->closing != 0) { return GS232B_ERROR_NOT_READY; }
    gs232b_wifi_sta_config_t config = adapter->configured;
    config.enabled = enabled;
    return gs232b_esp_wifi_sta_configure(adapter, &config);
}

gs232b_error_t gs232b_esp_wifi_sta_service(gs232b_esp_wifi_sta_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened || adapter->closing != 0) { return GS232B_ERROR_NOT_READY; }
    if (adapter->job != GS232B_STA_JOB_NONE) {
        gs232b_error_t result;
        if (xQueueReceive(adapter->results, &result, 0) != pdPASS) {
            if (adapter->job != GS232B_STA_JOB_SCAN) return GS232B_ERROR_WOULD_BLOCK;
        } else {
            if (adapter->job == GS232B_STA_JOB_SCAN) {
                adapter->scan_error = result;
                adapter->scan_done = true;
            }
            adapter->job = GS232B_STA_JOB_NONE;
        }
    }
    gs232b_sta_events_service(adapter);
    if (xSemaphoreTake(adapter->lock, 0) != pdPASS) { return GS232B_ERROR_WOULD_BLOCK; }
    const int64_t now = esp_timer_get_time();
    if (now >= 0 && (uint64_t)now >= adapter->waiting_since_us &&
        ((adapter->state.state == GS232B_WIFI_STA_CONNECTING &&
          (uint64_t)now - adapter->waiting_since_us >= GS232B_WIFI_STA_CONNECT_TIMEOUT_US) ||
         (adapter->state.state == GS232B_WIFI_STA_DHCP_WAIT &&
          (uint64_t)now - adapter->waiting_since_us >= GS232B_WIFI_STA_DHCP_TIMEOUT_US))) {
        gs232b_sta_retry(adapter, adapter->state.connected ?
            GS232B_WIFI_STA_FAILURE_DHCP_TIMEOUT : GS232B_WIFI_STA_FAILURE_CONNECT_TIMEOUT,
            GS232B_ERROR_TIMEOUT, ESP_ERR_TIMEOUT, 0);
    }
    const bool retry = adapter->job == GS232B_STA_JOB_NONE && adapter->state.enabled && now >= 0 &&
        adapter->state.state == GS232B_WIFI_STA_RETRY_WAIT &&
        (uint64_t)now >= adapter->state.retry_at_us;
    if (retry) { adapter->state.state = GS232B_WIFI_STA_STARTING; }
    const gs232b_error_t result = adapter->state.last_error;
    (void)xSemaphoreGive(adapter->lock);
    return retry ? dispatch(adapter, GS232B_STA_JOB_APPLY) : result;
}

gs232b_error_t gs232b_esp_wifi_sta_snapshot(gs232b_esp_wifi_sta_t *adapter,
                                          gs232b_wifi_sta_snapshot_t *snapshot)
{
    if (snapshot == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    memset(snapshot, 0, sizeof(*snapshot));
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened) { return GS232B_ERROR_NOT_READY; }
    if (xSemaphoreTake(adapter->lock, 0) != pdPASS) { return GS232B_ERROR_WOULD_BLOCK; }
    *snapshot = adapter->state;
    (void)xSemaphoreGive(adapter->lock);
    return GS232B_OK;
}

bool gs232b_esp_wifi_sta_interfaces_stable(const gs232b_esp_wifi_sta_t *adapter)
{
    return adapter != NULL && adapter->opened && adapter->closing == 0 &&
        (adapter->job == GS232B_STA_JOB_NONE || adapter->job == GS232B_STA_JOB_SCAN);
}

gs232b_error_t gs232b_esp_wifi_sta_close(gs232b_esp_wifi_sta_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened) { return GS232B_ERROR_NOT_READY; }
    if (adapter->closing == 0) {
        atomic_store(&adapter->cancelled, true);
        if (xSemaphoreTake(adapter->lock, 0) != pdPASS) { return GS232B_ERROR_WOULD_BLOCK; }
        adapter->accepting_events = false;
        gs232b_sta_invalidate(adapter);
        adapter->state.connected = false;
        adapter->closing = 1;
        (void)xSemaphoreGive(adapter->lock);
    }
    if (adapter->closing == 1) {
        if (adapter->job != GS232B_STA_JOB_NONE) {
            gs232b_error_t ignored;
            if (xQueueReceive(adapter->results, &ignored, 0) != pdPASS) {
                return GS232B_ERROR_WOULD_BLOCK;
            }
            adapter->job = GS232B_STA_JOB_NONE;
        }
        adapter->closing = 2;
    }
    if (adapter->closing == 2) {
        if (adapter->job == GS232B_STA_JOB_NONE) {
            const gs232b_error_t result = dispatch(adapter, GS232B_STA_JOB_STOP);
            return result == GS232B_OK ? GS232B_ERROR_WOULD_BLOCK : result;
        }
        gs232b_error_t result;
        if (xQueueReceive(adapter->results, &result, 0) != pdPASS) {
            return GS232B_ERROR_WOULD_BLOCK;
        }
        adapter->job = GS232B_STA_JOB_NONE;
        if (result != GS232B_OK) { return result; }
        const gs232b_error_t sent = dispatch(adapter, GS232B_STA_JOB_EXIT);
        if (sent != GS232B_OK) { return sent; }
        adapter->closing = 3;
    }
    if (adapter->closing == 3) {
        if (xSemaphoreTake(adapter->stopped, 0) != pdPASS) { return GS232B_ERROR_WOULD_BLOCK; }
        adapter->closing = 4;
    }
    return release(adapter);
}

static gs232b_error_t ap_start(void *ctx, const gs232b_provisioning_ap_config_t *config)
{
    return gs232b_esp_wifi_sta_ap_start(ctx, config);
}

static gs232b_error_t ap_stop(void *ctx)
{
    return gs232b_esp_wifi_sta_ap_stop(ctx);
}

static gs232b_error_t ap_scan(void *ctx, gs232b_provisioning_ap_t *aps,
    size_t capacity, size_t *count, bool *truncated)
{
    return gs232b_esp_wifi_sta_ap_scan(ctx, aps, capacity, count, truncated);
}

gs232b_provisioning_ap_port_t gs232b_esp_wifi_sta_ap_port(gs232b_esp_wifi_sta_t *adapter)
{
    return (gs232b_provisioning_ap_port_t){.ctx = adapter, .start = ap_start, .stop = ap_stop,
        .scan = ap_scan};
}

gs232b_error_t gs232b_esp_wifi_sta_ap_start(gs232b_esp_wifi_sta_t *adapter,
    const gs232b_provisioning_ap_config_t *config)
{
    const char *ssid_end = config == NULL ? NULL : memchr(config->ssid, '\0', sizeof(config->ssid));
    const char *password_end = config == NULL ? NULL : memchr(config->password, '\0', sizeof(config->password));
    const size_t ssid_length = ssid_end == NULL ? SIZE_MAX : (size_t)(ssid_end - config->ssid);
    const size_t password_length = password_end == NULL ? SIZE_MAX : (size_t)(password_end - config->password);
    if (adapter == NULL || config == NULL || ssid_end == NULL || password_end == NULL ||
        ssid_length == 0 || ssid_length > 32U || password_length < 8U ||
        password_length > 63U || config->address == 0 ||
        config->netmask == 0 || config->gateway != config->address) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!adapter->opened || adapter->closing != 0) return GS232B_ERROR_NOT_READY;
    if (adapter->job != GS232B_STA_JOB_NONE) return GS232B_ERROR_BUSY;
    adapter->ap_config = *config;
    adapter->ap_requested = true;
    const gs232b_error_t error = dispatch(adapter, GS232B_STA_JOB_AP_START);
    if (error != GS232B_OK) adapter->ap_requested = false;
    return error;
}

gs232b_error_t gs232b_esp_wifi_sta_ap_stop(gs232b_esp_wifi_sta_t *adapter)
{
    if (adapter == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    if (!adapter->opened || adapter->closing != 0) return GS232B_ERROR_NOT_READY;
    if (adapter->job != GS232B_STA_JOB_NONE) return GS232B_ERROR_BUSY;
    adapter->ap_requested = false;
    const gs232b_error_t error = dispatch(adapter, GS232B_STA_JOB_AP_STOP);
    if (error != GS232B_OK) adapter->ap_requested = true;
    return error;
}

static bool scan_ssid_valid(const wifi_ap_record_t *record)
{
    gs232b_wifi_sta_config_t network = {0};
    const size_t length = strnlen((const char *)record->ssid, sizeof(record->ssid));
    if (length == 0 || length > GS232B_WIFI_STA_SSID_MAX) return false;
    memcpy(network.ssid, record->ssid, length);
    return gs232b_wifi_sta_config_valid(&network);
}

gs232b_error_t gs232b_esp_wifi_sta_ap_scan(gs232b_esp_wifi_sta_t *adapter,
    gs232b_provisioning_ap_t *aps, size_t capacity, size_t *count, bool *truncated)
{
    if (aps == NULL || capacity == 0 || count == NULL || truncated == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *count = 0;
    *truncated = false;
    if (adapter == NULL || !adapter->opened || !adapter->ap_requested || adapter->closing != 0) {
        return GS232B_ERROR_NOT_READY;
    }
    if (adapter->job != GS232B_STA_JOB_NONE && adapter->job != GS232B_STA_JOB_SCAN)
        return GS232B_ERROR_BUSY;
    esp_err_t error = esp_wifi_scan_start(NULL, true);
    if (error != ESP_OK) {
        (void)esp_wifi_clear_ap_list();
        return gs232b_sta_error(error);
    }
    uint16_t found = 0;
    error = esp_wifi_scan_get_ap_num(&found);
    size_t stored = 0;
    for (uint16_t i = 0; error == ESP_OK && i < found; ++i) {
        wifi_ap_record_t record = {0};
        error = esp_wifi_scan_get_ap_record(&record);
        if (error != ESP_OK) break;
        if (!scan_ssid_valid(&record)) continue;
        if (stored == capacity) {
            *truncated = true;
            if (record.rssi <= aps[stored - 1U].rssi) continue;
        }
        size_t at = stored == capacity ? capacity - 1U : stored++;
        while (at != 0 && aps[at - 1U].rssi < record.rssi) {
            aps[at] = aps[at - 1U];
            --at;
        }
        aps[at] = (gs232b_provisioning_ap_t){.rssi = record.rssi, .auth = record.authmode};
        memcpy(aps[at].ssid, record.ssid,
            strnlen((const char *)record.ssid, sizeof(record.ssid)));
    }
    /* NOTE: 逐条取 SDK 结果，避免在管理任务栈上分配大型 wifi_ap_record_t 数组；任何退出都清理余项。 */
    const esp_err_t cleared = esp_wifi_clear_ap_list();
    if (error == ESP_OK) error = cleared;
    if (error != ESP_OK) { *truncated = false; return gs232b_sta_error(error); }
    *count = stored;
    return GS232B_OK;
}

gs232b_error_t gs232b_esp_wifi_sta_ap_scan_async(gs232b_esp_wifi_sta_t *adapter,
    gs232b_provisioning_ap_t *aps, size_t capacity, size_t *count, bool *truncated)
{
    if (adapter == NULL || aps == NULL || capacity == 0 || count == NULL || truncated == NULL)
        return GS232B_ERROR_INVALID_ARGUMENT;
    *count = 0;
    *truncated = false;
    if (!adapter->opened || !adapter->ap_requested || adapter->closing != 0)
        return GS232B_ERROR_NOT_READY;
    if (adapter->scan_done) {
        adapter->scan_done = false;
        if (adapter->scan_error != GS232B_OK) return adapter->scan_error;
        *count = adapter->scan_count < capacity ? adapter->scan_count : capacity;
        *truncated = adapter->scan_truncated || *count < adapter->scan_count;
        memcpy(aps, adapter->scan_aps, *count * sizeof(*aps));
        return GS232B_OK;
    }
    if (adapter->job == GS232B_STA_JOB_SCAN) return GS232B_ERROR_WOULD_BLOCK;
    if (adapter->job != GS232B_STA_JOB_NONE) return GS232B_ERROR_BUSY;
    const gs232b_error_t error = dispatch(adapter, GS232B_STA_JOB_SCAN);
    return error == GS232B_OK ? GS232B_ERROR_WOULD_BLOCK : error;
}
