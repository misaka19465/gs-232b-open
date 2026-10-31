#include "gs232b/nvs_store.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM

#include "nvs.h"
#include "nvs_flash.h"

#define GS232B_NVS_KEY_SIZE 16U

static bool domain_valid(gs232b_store_domain_t domain)
{
    return domain >= GS232B_STORE_NETWORK && domain < GS232B_STORE_DOMAIN_COUNT;
}

static gs232b_error_t map_error(esp_err_t error)
{
    return error == ESP_OK ? GS232B_OK : GS232B_ERROR_IO;
}

static void key_for(char key[GS232B_NVS_KEY_SIZE], gs232b_store_domain_t domain, char slot)
{
    (void)snprintf(key, GS232B_NVS_KEY_SIZE, "d%u%c", (unsigned)domain, slot);
}

static esp_err_t active_slot(gs232b_nvs_store_t *store,
                             gs232b_store_domain_t domain, uint8_t *slot)
{
    if (store->active_slot_valid[domain]) {
        *slot = store->active_slot[domain];
        return ESP_OK;
    }
    char key[GS232B_NVS_KEY_SIZE];
    key_for(key, domain, 's');
    esp_err_t error = nvs_get_u8(store->handle, key, slot);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        *slot = 0;
        store->active_slot[domain] = 0;
        store->active_slot_valid[domain] = true;
        return ESP_OK;
    }
    if (error == ESP_OK && *slot <= 1U) {
        store->active_slot[domain] = *slot;
        store->active_slot_valid[domain] = true;
        return ESP_OK;
    }
    return error == ESP_OK ? ESP_ERR_NVS_INVALID_STATE : error;
}

static void discard_pending(gs232b_nvs_store_t *store)
{
    if (store == NULL || !store->open) {
        return;
    }
    nvs_close(store->handle);
    store->open = false;
    store->handle = 0;
    if (nvs_open(store->namespace_name, NVS_READWRITE, &store->handle) == ESP_OK) {
        store->open = true;
        memset(store->active_slot_valid, 0, sizeof(store->active_slot_valid));
    }
}

static gs232b_error_t load_slot(void *ctx, gs232b_store_domain_t domain,
                                uint8_t slot, uint8_t *buffer, size_t capacity,
                                size_t *size)
{
    gs232b_nvs_store_t *store = ctx;
    if (size == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *size = 0;
    if (store == NULL || !store->open || !domain_valid(domain) ||
        (capacity != 0 && buffer == NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    char key[GS232B_NVS_KEY_SIZE];
    key_for(key, domain, slot == 0U ? 'a' : 'b');
    size_t required = 0;
    esp_err_t error = nvs_get_blob(store->handle, key, NULL, &required);
    if (error != ESP_OK) {
        return error == ESP_ERR_NVS_NOT_FOUND ? GS232B_ERROR_NOT_FOUND : map_error(error);
    }
    *size = required;
    if (required > capacity) {
        return GS232B_ERROR_BUFFER_TOO_SMALL;
    }
    error = nvs_get_blob(store->handle, key, buffer, &required);
    if (error != ESP_OK) {
        *size = 0;
        return map_error(error);
    }
    *size = required;
    return GS232B_OK;
}

static gs232b_error_t store_load(void *ctx, gs232b_store_domain_t domain,
                                uint8_t *buffer, size_t capacity, size_t *size)
{
    gs232b_nvs_store_t *store = ctx;
    if (store == NULL || !store->open || !domain_valid(domain)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    uint8_t slot = 0;
    esp_err_t error = active_slot(store, domain, &slot);
    if (error != ESP_OK) {
        return map_error(error);
    }
    return load_slot(ctx, domain, slot, buffer, capacity, size);
}

static gs232b_error_t store_load_previous(void *ctx, gs232b_store_domain_t domain,
                                         uint8_t *buffer, size_t capacity, size_t *size)
{
    gs232b_nvs_store_t *store = ctx;
    if (store == NULL || !store->open || !domain_valid(domain)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    uint8_t slot = 0;
    esp_err_t error = active_slot(store, domain, &slot);
    if (error != ESP_OK) {
        return map_error(error);
    }
    return load_slot(ctx, domain, (uint8_t)(slot ^ 1U), buffer, capacity, size);
}

static gs232b_error_t store_save(void *ctx, gs232b_store_domain_t domain,
                                const uint8_t *record, size_t size)
{
    gs232b_nvs_store_t *store = ctx;
    if (store == NULL || !store->open || !domain_valid(domain) ||
        (size != 0 && record == NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    uint8_t active = 0;
    esp_err_t error = active_slot(store, domain, &active);
    if (error != ESP_OK) {
        return map_error(error);
    }
    const uint8_t inactive = (uint8_t)(active ^ 1U);
    char key[GS232B_NVS_KEY_SIZE];
    key_for(key, domain, inactive == 0U ? 'a' : 'b');
    error = nvs_set_blob(store->handle, key, record, size);
    if (error != ESP_OK) {
        discard_pending(store);
        return map_error(error);
    }
    error = nvs_commit(store->handle);
    if (error != ESP_OK) {
        discard_pending(store);
        return map_error(error);
    }
    key_for(key, domain, 's');
    error = nvs_set_u8(store->handle, key, inactive);
    if (error != ESP_OK) {
        discard_pending(store);
        return map_error(error);
    }
    error = nvs_commit(store->handle);
    if (error != ESP_OK) {
        discard_pending(store);
        return map_error(error);
    }
    store->active_slot[domain] = inactive;
    store->active_slot_valid[domain] = true;
    return GS232B_OK;
}

static gs232b_error_t store_erase(void *ctx, gs232b_store_domain_t domain)
{
    gs232b_nvs_store_t *store = ctx;
    if (store == NULL || !store->open || !domain_valid(domain)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    char key[GS232B_NVS_KEY_SIZE];
    key_for(key, domain, 'a');
    esp_err_t error = nvs_erase_key(store->handle, key);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        discard_pending(store);
        return map_error(error);
    }
    key_for(key, domain, 'b');
    error = nvs_erase_key(store->handle, key);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        discard_pending(store);
        return map_error(error);
    }
    key_for(key, domain, 's');
    error = nvs_erase_key(store->handle, key);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        discard_pending(store);
        return map_error(error);
    }
    error = nvs_commit(store->handle);
    if (error != ESP_OK) {
        discard_pending(store);
        return map_error(error);
    }
    memset(store->active_slot_valid, 0, sizeof(store->active_slot_valid));
    return GS232B_OK;
}

gs232b_error_t gs232b_nvs_store_open(gs232b_nvs_store_t *store, const char *name)
{
    if (store == NULL || name == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (strlen(name) >= sizeof(store->namespace_name)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(store, 0, sizeof(*store));
    memcpy(store->namespace_name, name, strlen(name) + 1U);
    esp_err_t error = nvs_open(name, NVS_READWRITE, &store->handle);
    if (error != ESP_OK) {
        return map_error(error);
    }
    store->open = true;
    return GS232B_OK;
}

void gs232b_nvs_store_close(gs232b_nvs_store_t *store)
{
    if (store != NULL && store->open) {
        nvs_close(store->handle);
        store->open = false;
        store->handle = 0;
    }
}

gs232b_store_t gs232b_nvs_store_port(gs232b_nvs_store_t *store)
{
    return (gs232b_store_t){
        .ctx = store,
        .load = store_load,
        .load_previous = store_load_previous,
        .save = store_save,
        .erase = store_erase
    };
}

#else

gs232b_error_t gs232b_nvs_store_open(gs232b_nvs_store_t *store, const char *name)
{
    if (store == NULL || name == NULL || strlen(name) >= sizeof(store->namespace_name)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(store, 0, sizeof(*store));
    return GS232B_ERROR_IO;
}

void gs232b_nvs_store_close(gs232b_nvs_store_t *store)
{
    if (store != NULL) {
        memset(store, 0, sizeof(*store));
    }
}

gs232b_store_t gs232b_nvs_store_port(gs232b_nvs_store_t *store)
{
    (void)store;
    return (gs232b_store_t){0};
}

#endif
