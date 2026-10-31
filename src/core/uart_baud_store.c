#include "gs232b/uart_baud.h"

#define UART_BAUD_SCHEMA 1U

static uint32_t record_baud(const gs232b_persisted_record_t *record)
{
    if (record->schema_version != UART_BAUD_SCHEMA || record->payload_size != 4) { return 0; }
    const uint8_t *bytes = record->payload;
    const uint32_t baud = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
    return gs232b_uart_baud_valid(baud) ? baud : 0;
}

gs232b_error_t gs232b_uart_baud_load(const gs232b_store_t *store,
                                   uint32_t *baud, uint64_t *revision)
{
    if (baud == NULL || revision == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    *baud = GS232B_UART_DEFAULT_BAUD;
    *revision = 0;
    gs232b_persisted_record_t record;
    gs232b_error_t error = gs232b_persistence_load_schema(store, GS232B_STORE_UART_BAUD,
                                                        UART_BAUD_SCHEMA, &record);
    if (error != GS232B_OK) { return error; }
    uint32_t value = record_baud(&record);
    if (value == 0) {
        error = gs232b_persistence_load_previous(store, GS232B_STORE_UART_BAUD, &record);
        if (error != GS232B_OK) {
            return error == GS232B_ERROR_NOT_FOUND ? GS232B_ERROR_CORRUPT : error;
        }
        if ((value = record_baud(&record)) == 0) { return GS232B_ERROR_CORRUPT; }
    }
    *baud = value;
    *revision = record.revision;
    return GS232B_OK;
}

gs232b_error_t gs232b_uart_baud_commit(const gs232b_store_t *store,
    uint64_t expected_revision, uint32_t baud, uint64_t *new_revision)
{
    if (!gs232b_uart_baud_valid(baud)) {
        if (new_revision != NULL) { *new_revision = 0; }
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const uint8_t payload[] = {(uint8_t)baud, (uint8_t)(baud >> 8),
        (uint8_t)(baud >> 16), (uint8_t)(baud >> 24)};
    return gs232b_persistence_commit(store, GS232B_STORE_UART_BAUD, expected_revision,
                                     UART_BAUD_SCHEMA, payload, sizeof(payload), new_revision);
}
