#include "nvs_api.h"
#include "gs232b/persistence.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t blobs[GS232B_STORE_DOMAIN_COUNT][2][GS232B_PERSISTENCE_MAX_RECORD_SIZE];
    size_t sizes[GS232B_STORE_DOMAIN_COUNT][2];
    bool present[GS232B_STORE_DOMAIN_COUNT][2];
    uint8_t selectors[GS232B_STORE_DOMAIN_COUNT];
    bool selector_present[GS232B_STORE_DOMAIN_COUNT];
} fake_medium_t;

static fake_medium_t medium, pending;
/* NOTE: 替身只模拟提交边界的持久化；不把真实 Flash 掉电/半写入纳入主机结论。 */
static fake_nvs_stage_t stage;
static gs232b_error_t stage_error;
static bool is_open;
static fake_nvs_stage_t commit_kind;

void fake_nvs_reset(void)
{
    memset(&medium, 0, sizeof(medium));
    memset(&pending, 0, sizeof(pending));
    stage_error = GS232B_OK;
    is_open = false;
}

void fake_nvs_fail(fake_nvs_stage_t next, gs232b_error_t error)
{
    stage = next;
    stage_error = error;
}

static esp_err_t fail_at(fake_nvs_stage_t current)
{
    if (stage_error == GS232B_OK || stage != current) return ESP_OK;
    stage_error = GS232B_OK;
    return ESP_FAIL;
}

static bool parse(const char *key, gs232b_store_domain_t *domain, unsigned *slot)
{
    unsigned value;
    char kind;
    if (sscanf(key, "d%u%c", &value, &kind) != 2 || value >= GS232B_STORE_DOMAIN_COUNT)
        return false;
    *domain = (gs232b_store_domain_t)value;
    if (kind == 'a') *slot = 0;
    else if (kind == 'b') *slot = 1;
    else if (kind == 's') *slot = 2;
    else return false;
    return true;
}

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    (void)mode;
    if (name == NULL || handle == NULL || is_open) return ESP_ERR_INVALID_STATE;
    pending = medium;
    is_open = true;
    *handle = 1;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    assert(handle == 1 && is_open);
    is_open = false;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    gs232b_store_domain_t domain;
    unsigned slot;
    assert(handle == 1 && is_open && value != NULL);
    if (!parse(key, &domain, &slot) || slot != 2) return ESP_ERR_NVS_INVALID_STATE;
    if (!pending.selector_present[domain]) return ESP_ERR_NVS_NOT_FOUND;
    *value = pending.selectors[domain];
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *size)
{
    gs232b_store_domain_t domain;
    unsigned slot;
    assert(handle == 1 && is_open && size != NULL);
    if (!parse(key, &domain, &slot) || slot > 1) return ESP_ERR_NVS_INVALID_STATE;
    if (!pending.present[domain][slot]) return ESP_ERR_NVS_NOT_FOUND;
    if (value == NULL) { *size = pending.sizes[domain][slot]; return ESP_OK; }
    if (*size < pending.sizes[domain][slot]) return ESP_ERR_INVALID_ARG;
    *size = pending.sizes[domain][slot];
    memcpy(value, pending.blobs[domain][slot], *size);
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size)
{
    gs232b_store_domain_t domain;
    unsigned slot;
    assert(handle == 1 && is_open);
    if (fail_at(FAKE_NVS_SET_BLOB) != ESP_OK) return ESP_FAIL;
    if (!parse(key, &domain, &slot) || slot > 1 || value == NULL ||
        size > GS232B_PERSISTENCE_MAX_RECORD_SIZE) return ESP_ERR_INVALID_ARG;
    memcpy(pending.blobs[domain][slot], value, size);
    pending.sizes[domain][slot] = size;
    pending.present[domain][slot] = true;
    commit_kind = FAKE_NVS_COMMIT_DATA;
    return ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    gs232b_store_domain_t domain;
    unsigned slot;
    assert(handle == 1 && is_open);
    if (fail_at(FAKE_NVS_SET_SELECTOR) != ESP_OK) return ESP_FAIL;
    if (!parse(key, &domain, &slot) || slot != 2 || value > 1) return ESP_ERR_INVALID_ARG;
    pending.selectors[domain] = value;
    pending.selector_present[domain] = true;
    commit_kind = FAKE_NVS_COMMIT_SELECTOR;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    gs232b_store_domain_t domain;
    unsigned slot;
    assert(handle == 1 && is_open);
    if (fail_at(FAKE_NVS_ERASE) != ESP_OK) return ESP_FAIL;
    if (!parse(key, &domain, &slot)) return ESP_ERR_NVS_INVALID_STATE;
    if (slot == 2) pending.selector_present[domain] = false;
    else pending.present[domain][slot] = false;
    commit_kind = FAKE_NVS_COMMIT_ERASE;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1 && is_open);
    if (fail_at(commit_kind) != ESP_OK) return ESP_FAIL;
    medium = pending;
    return ESP_OK;
}
