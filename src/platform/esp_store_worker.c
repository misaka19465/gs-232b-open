#include "gs232b/esp_store_worker.h"

#include <string.h>
#include "nvs_flash.h"

enum { STORE_OPEN, STORE_SAVE, STORE_ERASE, STORE_EXIT };
typedef struct {
    unsigned kind;
    gs232b_store_domain_t domain;
    size_t size;
    uint8_t bytes[GS232B_PERSISTENCE_MAX_RECORD_SIZE];
} store_request_t;

static void run(void *ctx)
{
    gs232b_esp_store_worker_t *worker = ctx;
    store_request_t request;
    while (xQueueReceive(worker->requests, &request, portMAX_DELAY) == pdPASS) {
        gs232b_error_t error;
        gs232b_store_t raw = gs232b_nvs_store_port(&worker->nvs);
        if (request.kind == STORE_EXIT) {
            gs232b_nvs_store_close(&worker->nvs);
            error = GS232B_OK;
        } else if (request.kind == STORE_OPEN) {
            error = nvs_flash_init() == ESP_OK ?
                gs232b_nvs_store_open(&worker->nvs, "gs232b") : GS232B_ERROR_IO;
            if (error == GS232B_OK) {
                for (int i = 0; i < GS232B_STORE_DOMAIN_COUNT; ++i) {
                    gs232b_store_cache_t *current = &worker->current[i];
                    gs232b_store_cache_t *previous = &worker->previous[i];
                    current->error = gs232b_store_load(&raw, (gs232b_store_domain_t)i,
                        current->bytes, sizeof(current->bytes), &current->size);
                    previous->error = gs232b_store_load_previous(&raw, (gs232b_store_domain_t)i,
                        previous->bytes, sizeof(previous->bytes), &previous->size);
                }
            }
        } else if (request.kind == STORE_SAVE) {
            error = gs232b_store_save(&raw, request.domain, request.bytes, request.size);
        } else {
            error = gs232b_store_erase(&raw, request.domain);
        }
        /* NOTE: 不使用超时假装取消已开始的 Flash 提交；唯有实际完成才可回报成功。 */
        (void)xQueueSend(worker->results, &error, portMAX_DELAY);
        if (request.kind == STORE_EXIT) break;
    }
    vTaskDelete(NULL);
}

static gs232b_error_t transact(gs232b_esp_store_worker_t *worker, const store_request_t *request)
{
    if (worker->requests == NULL || xQueueSend(worker->requests, request, 0) != pdPASS) {
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    gs232b_error_t error;
    return xQueueReceive(worker->results, &error, portMAX_DELAY) == pdPASS ? error : GS232B_ERROR_IO;
}

gs232b_error_t gs232b_esp_store_worker_open(gs232b_esp_store_worker_t *worker,
    gs232b_motion_t *motion, gs232b_output_manager_t *outputs, const gs232b_control_t *control)
{
    if (worker == NULL || motion == NULL || outputs == NULL || worker->task != NULL ||
        worker->requests != NULL || worker->results != NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    worker->motion = motion;
    worker->outputs = outputs;
    worker->control = control;
    worker->open_error = GS232B_ERROR_RESOURCE_EXHAUSTED;
    for (int i = 0; i < GS232B_STORE_DOMAIN_COUNT; ++i) {
        worker->current[i].error = worker->previous[i].error = GS232B_ERROR_NOT_FOUND;
    }
    worker->requests = xQueueCreate(1, sizeof(store_request_t));
    worker->results = xQueueCreate(1, sizeof(gs232b_error_t));
    if (worker->requests == NULL || worker->results == NULL ||
        xTaskCreate(run, "gs_store", 4096, worker, 2, &worker->task) != pdPASS) {
        if (worker->requests != NULL) vQueueDelete(worker->requests);
        if (worker->results != NULL) vQueueDelete(worker->results);
        worker->requests = worker->results = NULL;
        return worker->open_error;
    }
    const store_request_t request = {.kind = STORE_OPEN};
    worker->open_error = transact(worker, &request);
    return worker->open_error;
}

static gs232b_error_t read_cache(gs232b_store_cache_t *cache,
    uint8_t *buffer, size_t capacity, size_t *size)
{
    *size = 0;
    if (cache->error == GS232B_ERROR_BUFFER_TOO_SMALL) {
        *size = cache->size;
        return cache->size > capacity ? GS232B_ERROR_BUFFER_TOO_SMALL : GS232B_ERROR_CORRUPT;
    }
    if (cache->error != GS232B_OK) return cache->error;
    *size = cache->size;
    if (capacity < cache->size) return GS232B_ERROR_BUFFER_TOO_SMALL;
    if (cache->size != 0) memcpy(buffer, cache->bytes, cache->size);
    return GS232B_OK;
}

static gs232b_error_t load(void *ctx, gs232b_store_domain_t domain,
    uint8_t *buffer, size_t capacity, size_t *size)
{
    gs232b_esp_store_worker_t *worker = ctx;
    *size = 0;
    if (domain < 0 || domain >= GS232B_STORE_DOMAIN_COUNT) return GS232B_ERROR_INVALID_ARGUMENT;
    return worker->open_error == GS232B_OK ?
        read_cache(&worker->current[domain], buffer, capacity, size) : GS232B_ERROR_NOT_FOUND;
}

static gs232b_error_t previous(void *ctx, gs232b_store_domain_t domain,
    uint8_t *buffer, size_t capacity, size_t *size)
{
    gs232b_esp_store_worker_t *worker = ctx;
    *size = 0;
    if (domain < 0 || domain >= GS232B_STORE_DOMAIN_COUNT) return GS232B_ERROR_INVALID_ARGUMENT;
    return worker->open_error == GS232B_OK ?
        read_cache(&worker->previous[domain], buffer, capacity, size) : GS232B_ERROR_NOT_FOUND;
}

static bool stopped(const gs232b_esp_store_worker_t *worker)
{
    if (worker->control != NULL && worker->control->initialized &&
        worker->control->sequence_state.state == GS232B_SEQUENCE_RUNNING) return false;
    gs232b_output_manager_snapshot_t output;
    if (gs232b_output_manager_snapshot(worker->outputs, &output) != GS232B_OK ||
        !output.output.known || output.requested.azimuth != GS232B_AZ_STOP ||
        output.requested.elevation != GS232B_EL_STOP || output.requested.speed_permille != 0 ||
        output.output.last_successful.azimuth != GS232B_AZ_STOP ||
        output.output.last_successful.elevation != GS232B_EL_STOP ||
        output.output.last_successful.speed_permille != 0) return false;
    if (worker->motion->initialized) {
        for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
            const gs232b_axis_mode_t mode = worker->motion->axes[i].intent;
            if (mode == GS232B_AXIS_MANUAL || mode == GS232B_AXIS_GOTO) return false;
        }
    }
    return true;
}

static gs232b_error_t write_record(gs232b_esp_store_worker_t *worker,
    const store_request_t *request)
{
    if (worker->open_error != GS232B_OK) return worker->open_error;
    if (!stopped(worker)) return GS232B_ERROR_BUSY;
    const gs232b_error_t error = transact(worker, request);
    if (error != GS232B_OK) return error;
    gs232b_store_cache_t *current = &worker->current[request->domain];
    if (request->kind == STORE_SAVE) {
        worker->previous[request->domain] = *current;
        *current = (gs232b_store_cache_t){.error = GS232B_OK, .size = request->size};
        memcpy(current->bytes, request->bytes, request->size);
    } else {
        *current = (gs232b_store_cache_t){.error = GS232B_ERROR_NOT_FOUND};
        worker->previous[request->domain] = *current;
    }
    return GS232B_OK;
}

static gs232b_error_t save(void *ctx, gs232b_store_domain_t domain, const uint8_t *bytes, size_t size)
{
    if (size > GS232B_PERSISTENCE_MAX_RECORD_SIZE) return GS232B_ERROR_BUFFER_TOO_SMALL;
    store_request_t request = {.kind = STORE_SAVE, .domain = domain, .size = size};
    if (size != 0) memcpy(request.bytes, bytes, size);
    return write_record(ctx, &request);
}

static gs232b_error_t erase(void *ctx, gs232b_store_domain_t domain)
{
    const store_request_t request = {.kind = STORE_ERASE, .domain = domain};
    return write_record(ctx, &request);
}

gs232b_store_t gs232b_esp_store_worker_port(gs232b_esp_store_worker_t *worker)
{
    return (gs232b_store_t){.ctx = worker, .load = load, .load_previous = previous,
        .save = save, .erase = erase};
}

gs232b_error_t gs232b_esp_store_worker_close(gs232b_esp_store_worker_t *worker)
{
    if (worker == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    if (worker->task == NULL) return GS232B_OK;
    if (!stopped(worker)) return GS232B_ERROR_BUSY;
    const store_request_t request = {.kind = STORE_EXIT};
    const gs232b_error_t error = transact(worker, &request);
    if (error != GS232B_OK) return error;
    /* NOTE: worker 在发布完成后不再接触队列或存储实例。 */
    vQueueDelete(worker->requests);
    vQueueDelete(worker->results);
    memset(worker, 0, sizeof(*worker));
    return GS232B_OK;
}
