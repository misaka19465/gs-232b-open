#include "esp_wifi_sta_internal.h"

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
        gs232b_error_t result = gs232b_sta_stop(adapter);
        if (result == GS232B_OK && job == GS232B_STA_JOB_APPLY &&
            adapter->configured.enabled && !atomic_load(&adapter->cancelled)) {
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
    if (adapter->fence != NULL) { vSemaphoreDelete(adapter->fence); }
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
    adapter->fence = xSemaphoreCreateBinary();
    adapter->stopped = xSemaphoreCreateBinary();
    adapter->requests = xQueueCreate(1, sizeof(unsigned));
    adapter->results = xQueueCreate(1, sizeof(gs232b_error_t));
    if (adapter->lock == NULL || adapter->fence == NULL || adapter->stopped == NULL ||
        adapter->requests == NULL || adapter->results == NULL) {
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
            return GS232B_ERROR_WOULD_BLOCK;
        }
        adapter->job = GS232B_STA_JOB_NONE;
    }
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
    const bool retry = adapter->state.enabled && now >= 0 &&
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
