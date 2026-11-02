#include "esp_ethernet_internal.h"

#include <string.h>

#include "esp_mac.h"
#include "esp_timer.h"

gs232b_error_t gs232b_eth_error(esp_err_t error)
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

static bool recovering(const gs232b_esp_ethernet_t *adapter)
{
    return adapter->reset.phase >= GS232B_W5500_QUIESCING &&
           adapter->reset.phase <= GS232B_W5500_RESUMING;
}

static gs232b_error_t dispatch(gs232b_esp_ethernet_t *adapter, unsigned job)
{
    if (adapter->job == GS232B_ETH_JOB_NONE) {
        if (xQueueSend(adapter->requests, &job, 0) != pdPASS) {
            return GS232B_ERROR_BUSY;
        }
        adapter->job = job;
        return GS232B_ERROR_WOULD_BLOCK;
    }
    if (adapter->job != job) { return GS232B_ERROR_BUSY; }
    gs232b_error_t result;
    if (xQueueReceive(adapter->results, &result, 0) != pdPASS) {
        return GS232B_ERROR_WOULD_BLOCK;
    }
    adapter->job = GS232B_ETH_JOB_NONE;
    return result;
}

static gs232b_error_t quiesce(void *ctx)
{
    return dispatch(ctx, GS232B_ETH_JOB_QUIESCE);
}

static gs232b_error_t resume(void *ctx)
{
    return dispatch(ctx, GS232B_ETH_JOB_RESUME);
}

static void worker(void *ctx)
{
    gs232b_esp_ethernet_t *adapter = ctx;
    unsigned job;
    while (xQueueReceive(adapter->requests, &job, portMAX_DELAY) == pdPASS) {
        if (job == GS232B_ETH_JOB_EXIT) { break; }
        const gs232b_error_t result = job == GS232B_ETH_JOB_QUIESCE ?
            gs232b_eth_driver_quiesce(adapter) : gs232b_eth_driver_resume(adapter);
        if (result != GS232B_OK) {
            (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
            adapter->accepting_events = false;
            gs232b_eth_fail(adapter, result);
            (void)xSemaphoreGive(adapter->lock);
        }
        (void)xQueueSend(adapter->results, &result, portMAX_DELAY);
    }
    (void)xSemaphoreGive(adapter->stopped);
    vTaskDelete(NULL);
}

static gs232b_error_t release_service(gs232b_esp_ethernet_t *adapter)
{
    if (adapter->fence_handler != NULL) {
        const esp_err_t error = esp_event_handler_instance_unregister(
            GS232B_ETH_FENCE_EVENT, 0, adapter->fence_handler);
        if (error != ESP_OK) { return gs232b_eth_error(error); }
        adapter->fence_handler = NULL;
    }
    if (adapter->requests != NULL) { vQueueDelete(adapter->requests); }
    if (adapter->results != NULL) { vQueueDelete(adapter->results); }
    if (adapter->fence != NULL) { vQueueDelete(adapter->fence); }
    if (adapter->events != NULL) { vQueueDelete(adapter->events); }
    if (adapter->stopped != NULL) { vSemaphoreDelete(adapter->stopped); }
    if (adapter->lock != NULL) { vSemaphoreDelete(adapter->lock); }
    memset(adapter, 0, sizeof(*adapter));
    return GS232B_OK;
}

static gs232b_error_t open_service(gs232b_esp_ethernet_t *adapter,
                                      gs232b_shared_spi_t *bus,
                                      gs232b_output_manager_t *outputs,
                                      const gs232b_ethernet_config_t *config, bool reset_ready)
{
    const gs232b_ethernet_config_t dhcp = {0};
    if (config == NULL) { config = &dhcp; }
    if (adapter == NULL || bus == NULL || adapter->opened || adapter->lock != NULL ||
        !gs232b_ethernet_config_valid(config) ||
        atomic_load(&bus->ethernet_enabled) != reset_ready) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_error_t result = gs232b_w5500_reset_init(&adapter->reset, outputs, bus,
        (gs232b_w5500_lifecycle_t){adapter, quiesce, resume},
        GS232B_W5500_RESET_DEFAULT_US, GS232B_W5500_RECOVERY_DEFAULT_US);
    if (result != GS232B_OK) { return result; }
    if (!outputs->state.output.known) { return GS232B_ERROR_NOT_READY; }
    esp_err_t error = esp_netif_init();
    if (error != ESP_OK) { return gs232b_eth_error(error); }
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return gs232b_eth_error(error);
    }
    error = esp_read_mac(adapter->state.mac, ESP_MAC_ETH);
    if (error != ESP_OK) { return gs232b_eth_error(error); }
    if ((adapter->state.mac[0] & 1U) != 0 ||
        (adapter->state.mac[0] | adapter->state.mac[1] | adapter->state.mac[2] |
         adapter->state.mac[3] | adapter->state.mac[4] | adapter->state.mac[5]) == 0) {
        return GS232B_ERROR_HARDWARE_FAULT;
    }
    adapter->state.configured = *config;
    adapter->state.session_epoch = 1;
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
        (void)release_service(adapter);
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    error = esp_event_handler_instance_register(GS232B_ETH_FENCE_EVENT, 0,
        gs232b_eth_fence_event, adapter, &adapter->fence_handler);
    result = gs232b_eth_error(error);
    if (result == GS232B_OK && xTaskCreate(worker, "gs_eth", 4096, adapter, 3,
                                         &adapter->worker) != pdPASS) {
        result = GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    if (result != GS232B_OK) { (void)release_service(adapter); return result; }
    adapter->opened = true;
    if (reset_ready) adapter->reset.phase = GS232B_W5500_RESUMING;
    return GS232B_OK;
}

gs232b_error_t gs232b_esp_ethernet_open(gs232b_esp_ethernet_t *adapter,
    gs232b_shared_spi_t *bus, gs232b_output_manager_t *outputs,
    const gs232b_ethernet_config_t *config)
{
    return open_service(adapter, bus, outputs, config, false);
}

gs232b_error_t gs232b_esp_ethernet_open_after_reset(gs232b_esp_ethernet_t *adapter,
    const gs232b_w5500_reset_t *startup, const gs232b_ethernet_config_t *config)
{
    if (startup == NULL || startup->phase != GS232B_W5500_READY || startup->outputs == NULL) {
        return GS232B_ERROR_NOT_READY;
    }
    const gs232b_output_snapshot_t *output = &startup->outputs->state.output;
    if (!output->known || output->last_successful.w5500_reset_asserted ||
        output->last_successful.azimuth != GS232B_AZ_STOP ||
        output->last_successful.elevation != GS232B_EL_STOP ||
        output->last_successful.speed_permille != 0) return GS232B_ERROR_NOT_READY;
    return open_service(adapter, startup->bus, startup->outputs, config, true);
}

gs232b_error_t gs232b_esp_ethernet_recover(gs232b_esp_ethernet_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened || adapter->closing != 0) { return GS232B_ERROR_NOT_READY; }
    if (recovering(adapter) || adapter->job != GS232B_ETH_JOB_NONE) {
        return GS232B_ERROR_BUSY;
    }
    if (!adapter->reset.outputs->state.output.known) { return GS232B_ERROR_NOT_READY; }
    if (xSemaphoreTake(adapter->lock, 0) != pdPASS) { return GS232B_ERROR_WOULD_BLOCK; }
    if (adapter->state.session_epoch == UINT64_MAX) {
        (void)xSemaphoreGive(adapter->lock);
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    adapter->accepting_events = false;
    atomic_store(&adapter->cancelled, false);
    gs232b_eth_invalidate(adapter);
    if (adapter->state.session_epoch == UINT64_MAX) {
        (void)xSemaphoreGive(adapter->lock);
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    adapter->state.link_up = false;
    adapter->state.state = GS232B_ETHERNET_LINK_DOWN;
    adapter->state.last_error = GS232B_OK;
    (void)xSemaphoreGive(adapter->lock);
    return gs232b_w5500_reset_begin(&adapter->reset);
}

gs232b_error_t gs232b_esp_ethernet_configure(gs232b_esp_ethernet_t *adapter,
                                           const gs232b_ethernet_config_t *config)
{
    if (adapter == NULL || !gs232b_ethernet_config_valid(config)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!adapter->opened || adapter->closing != 0) { return GS232B_ERROR_NOT_READY; }
    if (recovering(adapter) || adapter->job != GS232B_ETH_JOB_NONE) {
        return GS232B_ERROR_BUSY;
    }
    if (!adapter->reset.outputs->state.output.known) { return GS232B_ERROR_NOT_READY; }
    if (xSemaphoreTake(adapter->lock, 0) != pdPASS) { return GS232B_ERROR_WOULD_BLOCK; }
    if (adapter->state.session_epoch == UINT64_MAX) {
        (void)xSemaphoreGive(adapter->lock);
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    adapter->state.configured = *config;
    adapter->accepting_events = false;
    atomic_store(&adapter->cancelled, false);
    gs232b_eth_invalidate(adapter);
    if (adapter->state.session_epoch == UINT64_MAX) {
        (void)xSemaphoreGive(adapter->lock);
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    adapter->state.link_up = false;
    adapter->state.state = GS232B_ETHERNET_LINK_DOWN;
    adapter->state.last_error = GS232B_OK;
    (void)xSemaphoreGive(adapter->lock);
    return gs232b_w5500_reset_begin(&adapter->reset);
}

gs232b_error_t gs232b_esp_ethernet_service(gs232b_esp_ethernet_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened || adapter->closing != 0) { return GS232B_ERROR_NOT_READY; }
    const gs232b_error_t result = gs232b_w5500_reset_tick(&adapter->reset);
    if (result != GS232B_OK) {
        /* NOTE: 输出故障可在异步 resume 完成前中止协调器；消费迟到结果，
         * 防止 job 永久 BUSY，取消标志阻止工作任务再接纳旧事件。 */
        atomic_store(&adapter->cancelled, true);
        if (adapter->job != GS232B_ETH_JOB_NONE) {
            gs232b_error_t ignored;
            if (xQueueReceive(adapter->results, &ignored, 0) == pdPASS) {
                adapter->job = GS232B_ETH_JOB_NONE;
            }
        }
    }
    if (adapter->job == GS232B_ETH_JOB_NONE) gs232b_eth_events_service(adapter);
    if (xSemaphoreTake(adapter->lock, 0) != pdPASS) {
        return result != GS232B_OK ? result : GS232B_ERROR_WOULD_BLOCK;
    }
    if (result != GS232B_OK) {
        adapter->accepting_events = false;
        gs232b_eth_fail(adapter, adapter->reset.last_error);
    } else if (adapter->state.link_up && !adapter->state.ipv4_valid &&
               adapter->state.configured.mode == GS232B_ETHERNET_DHCP) {
        const int64_t now = esp_timer_get_time();
        if (now >= 0 && (uint64_t)now >= adapter->waiting_since_us &&
            (uint64_t)now - adapter->waiting_since_us >= GS232B_ETHERNET_DHCP_TIMEOUT_US) {
            gs232b_eth_fail(adapter, GS232B_ERROR_TIMEOUT);
        }
    }
    const gs232b_error_t state_error = adapter->state.last_error;
    (void)xSemaphoreGive(adapter->lock);
    return result != GS232B_OK ? result : state_error;
}

gs232b_error_t gs232b_esp_ethernet_snapshot(gs232b_esp_ethernet_t *adapter,
                                          gs232b_ethernet_snapshot_t *snapshot)
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

gs232b_error_t gs232b_esp_ethernet_close(gs232b_esp_ethernet_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened) { return release_service(adapter); }
    if (adapter->closing == 0) {
        if (xSemaphoreTake(adapter->lock, 0) != pdPASS) { return GS232B_ERROR_WOULD_BLOCK; }
        adapter->closing = 1;
        atomic_store(&adapter->cancelled, true);
        adapter->accepting_events = false;
        gs232b_eth_invalidate(adapter);
        adapter->state.link_up = false;
        adapter->state.state = GS232B_ETHERNET_LINK_DOWN;
        (void)xSemaphoreGive(adapter->lock);
    }
    if (adapter->closing == 1) {
        if (adapter->job != GS232B_ETH_JOB_NONE) {
            gs232b_error_t ignored;
            if (xQueueReceive(adapter->results, &ignored, 0) != pdPASS) {
                return GS232B_ERROR_WOULD_BLOCK;
            }
            adapter->job = GS232B_ETH_JOB_NONE;
        }
        adapter->closing = 2;
    }
    if (adapter->closing == 2) {
        const gs232b_error_t result = dispatch(adapter, GS232B_ETH_JOB_QUIESCE);
        if (result != GS232B_OK) { return result; }
        const gs232b_error_t gate = gs232b_shared_spi_set_ethernet_enabled(
            adapter->reset.bus, false);
        if (gate != GS232B_OK) { return gate; }
        const unsigned job = GS232B_ETH_JOB_EXIT;
        if (xQueueSend(adapter->requests, &job, 0) != pdPASS) {
            return GS232B_ERROR_BUSY;
        }
        adapter->closing = 3;
    }
    if (adapter->closing == 3) {
        if (xSemaphoreTake(adapter->stopped, 0) != pdPASS) {
            return GS232B_ERROR_WOULD_BLOCK;
        }
        adapter->closing = 4;
    }
    return release_service(adapter);
}
