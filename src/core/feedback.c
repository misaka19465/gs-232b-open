#include "gs232b/feedback.h"

#include <string.h>

#define GS232B_ZERO_RECORD_MAGIC_0 UINT8_C(0x47)
#define GS232B_ZERO_RECORD_MAGIC_1 UINT8_C(0x5a)
#define GS232B_ZERO_RECORD_VERSION UINT8_C(1)
#define GS232B_ZERO_RECORD_PAYLOAD_SIZE 8U

static bool axis_valid(gs232b_axis_id_t axis)
{
    return axis == GS232B_AXIS_AZIMUTH || axis == GS232B_AXIS_ELEVATION;
}

static gs232b_store_domain_t zero_domain(gs232b_axis_id_t axis)
{
    return axis == GS232B_AXIS_AZIMUTH ? GS232B_STORE_ZERO_AZ : GS232B_STORE_ZERO_EL;
}

static uint16_t crc16(const uint8_t *bytes, size_t size)
{
    uint16_t crc = UINT16_C(0xffff);
    for (size_t i = 0; i < size; ++i) {
        crc ^= (uint16_t)bytes[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & UINT16_C(0x8000)) != 0
                      ? (uint16_t)((crc << 1) ^ UINT16_C(0x1021))
                      : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static bool fresh(gs232b_time_us_t acquired_at_us, gs232b_time_us_t now_us)
{
    return acquired_at_us <= now_us &&
           now_us - acquired_at_us <= GS232B_FEEDBACK_SAMPLE_MAX_AGE_US;
}

void gs232b_feedback_filter_init(gs232b_feedback_filter_t *filter)
{
    if (filter != NULL) {
        memset(filter, 0, sizeof(*filter));
    }
}

gs232b_error_t gs232b_feedback_sample_validate(const gs232b_sample_t *sample,
                                               gs232b_time_us_t now_us)
{
    if (sample == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!sample->valid || !fresh(sample->acquired_at_us, now_us)) {
        return GS232B_ERROR_NOT_READY;
    }
    if (sample->adc_uv > GS232B_FEEDBACK_FULL_SCALE_UV) {
        return GS232B_ERROR_HARDWARE_FAULT;
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_feedback_filter_push(gs232b_feedback_filter_t *filter,
                                            const gs232b_sample_t *sample,
                                            gs232b_time_us_t now_us)
{
    if (filter == NULL || sample == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_error_t error = gs232b_feedback_sample_validate(sample, now_us);
    if (error != GS232B_OK) {
        if (error == GS232B_ERROR_HARDWARE_FAULT) {
            gs232b_feedback_filter_init(filter);
        }
        return error;
    }
    if (filter->count != 0 && sample->acquired_at_us < filter->latest_at_us) {
        return GS232B_ERROR_NOT_READY;
    }
    if (filter->count != 0 &&
        sample->acquired_at_us - filter->latest_at_us >
            GS232B_FEEDBACK_SAMPLE_MAX_AGE_US) {
        gs232b_feedback_filter_init(filter);
    }

    filter->values_uv[filter->next] = sample->adc_uv;
    filter->next = (filter->next + 1U) % GS232B_FEEDBACK_FILTER_WINDOW;
    if (filter->count < GS232B_FEEDBACK_FILTER_WINDOW) {
        ++filter->count;
    }
    filter->latest_at_us = sample->acquired_at_us;
    return GS232B_OK;
}

gs232b_error_t gs232b_feedback_filter_read(const gs232b_feedback_filter_t *filter,
                                            gs232b_time_us_t now_us,
                                            gs232b_feedback_reading_t *reading)
{
    if (reading == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(reading, 0, sizeof(*reading));
    if (filter == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (filter->count == 0 || filter->count > GS232B_FEEDBACK_FILTER_WINDOW ||
        !fresh(filter->latest_at_us, now_us)) {
        return GS232B_ERROR_NOT_READY;
    }

    uint32_t sorted[GS232B_FEEDBACK_FILTER_WINDOW];
    memcpy(sorted, filter->values_uv, filter->count * sizeof(sorted[0]));
    for (size_t i = 1; i < filter->count; ++i) {
        const uint32_t value = sorted[i];
        size_t j = i;
        while (j > 0 && sorted[j - 1] > value) {
            sorted[j] = sorted[j - 1];
            --j;
        }
        sorted[j] = value;
    }

    const size_t first = filter->count >= 3U ? 1U : 0U;
    const size_t end = filter->count >= 3U ? filter->count - 1U : filter->count;
    uint64_t sum = 0;
    for (size_t i = first; i < end; ++i) {
        sum += sorted[i];
    }
    const size_t count = end - first;
    reading->adc_uv = (uint32_t)((sum + count / 2U) / count);
    reading->acquired_at_us = filter->latest_at_us;
    reading->valid = true;
    return GS232B_OK;
}

gs232b_error_t gs232b_feedback_zero_validate(gs232b_axis_id_t axis,
                                              const gs232b_feedback_reading_t *reading,
                                              gs232b_time_us_t now_us)
{
    if (!axis_valid(axis) || reading == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!reading->valid || !fresh(reading->acquired_at_us, now_us)) {
        return GS232B_ERROR_NOT_READY;
    }
    if (reading->adc_uv > GS232B_FEEDBACK_FULL_SCALE_UV ||
        GS232B_FEEDBACK_FULL_SCALE_UV - reading->adc_uv <
            GS232B_FEEDBACK_MIN_SPAN_UV) {
        return GS232B_ERROR_HARDWARE_FAULT;
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_feedback_zero_save(const gs232b_store_t *store,
                                          gs232b_axis_id_t axis,
                                          const gs232b_feedback_reading_t *reading,
                                          gs232b_time_us_t now_us)
{
    if (store == NULL || !axis_valid(axis)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_error_t error = gs232b_feedback_zero_validate(axis, reading, now_us);
    if (error != GS232B_OK) {
        return error;
    }

    uint8_t record[GS232B_FEEDBACK_ZERO_RECORD_SIZE] = {
        GS232B_ZERO_RECORD_MAGIC_0,
        GS232B_ZERO_RECORD_MAGIC_1,
        GS232B_ZERO_RECORD_VERSION,
        (uint8_t)axis,
        (uint8_t)(reading->adc_uv & UINT32_C(0xff)),
        (uint8_t)((reading->adc_uv >> 8) & UINT32_C(0xff)),
        (uint8_t)((reading->adc_uv >> 16) & UINT32_C(0xff)),
        (uint8_t)((reading->adc_uv >> 24) & UINT32_C(0xff)),
        0,
        0
    };
    const uint16_t checksum = crc16(record, GS232B_ZERO_RECORD_PAYLOAD_SIZE);
    record[8] = (uint8_t)(checksum & UINT16_C(0xff));
    record[9] = (uint8_t)(checksum >> 8);
    return gs232b_store_save(store, zero_domain(axis), record, sizeof(record));
}

gs232b_error_t gs232b_feedback_zero_load(const gs232b_store_t *store,
                                          gs232b_axis_id_t axis,
                                          uint32_t *zero_uv)
{
    if (zero_uv == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *zero_uv = 0;
    if (store == NULL || !axis_valid(axis)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }

    uint8_t record[GS232B_FEEDBACK_ZERO_RECORD_SIZE];
    size_t size = 0;
    gs232b_error_t error = gs232b_store_load(store, zero_domain(axis), record,
                                              sizeof(record), &size);
    if (error == GS232B_ERROR_BUFFER_TOO_SMALL) {
        return GS232B_ERROR_HARDWARE_FAULT;
    }
    if (error != GS232B_OK) {
        return error == GS232B_ERROR_NOT_FOUND ? GS232B_ERROR_NOT_READY : error;
    }
    if (size != sizeof(record) || record[0] != GS232B_ZERO_RECORD_MAGIC_0 ||
        record[1] != GS232B_ZERO_RECORD_MAGIC_1 ||
        record[2] != GS232B_ZERO_RECORD_VERSION || record[3] != (uint8_t)axis ||
        crc16(record, GS232B_ZERO_RECORD_PAYLOAD_SIZE) !=
            (uint16_t)((uint16_t)record[8] | ((uint16_t)record[9] << 8))) {
        return GS232B_ERROR_HARDWARE_FAULT;
    }

    const uint32_t candidate = (uint32_t)record[4] |
                               ((uint32_t)record[5] << 8) |
                               ((uint32_t)record[6] << 16) |
                               ((uint32_t)record[7] << 24);
    if (candidate > GS232B_FEEDBACK_FULL_SCALE_UV ||
        GS232B_FEEDBACK_FULL_SCALE_UV - candidate < GS232B_FEEDBACK_MIN_SPAN_UV) {
        return GS232B_ERROR_HARDWARE_FAULT;
    }
    *zero_uv = candidate;
    return GS232B_OK;
}
