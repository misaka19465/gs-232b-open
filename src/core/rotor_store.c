#include "gs232b/rotor.h"
#include "gs232b/persistence.h"

static uint16_t decode(const gs232b_persisted_record_t *record)
{
    if (record->schema_version != GS232B_PERSISTENCE_SCHEMA_ROTOR ||
        record->payload_size != 2U) return 0;
    const uint16_t id = (uint16_t)((uint16_t)record->payload[0] |
        ((uint16_t)record->payload[1] << 8));
    return gs232b_rotor_model_valid(id) ? id : 0;
}

gs232b_error_t gs232b_rotor_load(const gs232b_store_t *store,
    uint16_t *model_id, uint64_t *revision)
{
    if (model_id == NULL || revision == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    *model_id = 0;
    *revision = 0;
    gs232b_persisted_record_t record;
    gs232b_error_t error = gs232b_persistence_load(store, GS232B_STORE_ROTOR, &record);
    if (error == GS232B_ERROR_NOT_FOUND) {
        *model_id = GS232B_ROTOR_DEFAULT_MODEL_ID;
        return GS232B_OK;
    }
    if (error != GS232B_OK) return error;
    const uint64_t current_revision = record.revision;
    uint16_t id = decode(&record);
    if (id == 0) {
        error = gs232b_persistence_load_previous(store, GS232B_STORE_ROTOR, &record);
        if (error != GS232B_OK) return error == GS232B_ERROR_NOT_FOUND ? GS232B_ERROR_CORRUPT : error;
        id = decode(&record);
        if (id == 0) return GS232B_ERROR_CORRUPT;
    }
    *model_id = id;
    *revision = current_revision;
    return GS232B_OK;
}

gs232b_error_t gs232b_rotor_save(const gs232b_store_t *store,
    uint64_t expected_revision, uint16_t model_id, uint64_t *new_revision)
{
    if (new_revision == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    *new_revision = 0;
    if (!gs232b_rotor_model_valid(model_id)) return GS232B_ERROR_INVALID_ARGUMENT;
    /* NOTE: 独立域仅写入两字节 little-endian 数字 ID，不持久化原生配置结构。 */
    const uint8_t payload[] = {(uint8_t)model_id, (uint8_t)(model_id >> 8)};
    return gs232b_persistence_commit(store, GS232B_STORE_ROTOR, expected_revision,
        GS232B_PERSISTENCE_SCHEMA_ROTOR, payload, sizeof(payload), new_revision);
}
