#include "gs232b/feedback.h"
#include "gs232b/fakes.h"

#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static gs232b_feedback_reading_t reading(uint32_t adc_uv, gs232b_time_us_t at_us)
{
    return (gs232b_feedback_reading_t){
        .acquired_at_us = at_us,
        .adc_uv = adc_uv,
        .valid = true
    };
}

static gs232b_sample_t sample(uint32_t adc_uv, gs232b_time_us_t at_us, bool valid)
{
    return (gs232b_sample_t){
        .acquired_at_us = at_us,
        .raw = 4095,
        .adc_uv = adc_uv,
        .valid = valid
    };
}

static bool test_filter_boundaries_and_freshness(void)
{
    gs232b_feedback_filter_t filter;
    gs232b_feedback_filter_init(&filter);
    gs232b_feedback_reading_t value;
    CHECK(gs232b_feedback_filter_read(&filter, 0, &value) == GS232B_ERROR_NOT_READY);
    CHECK(!value.valid);

    const uint32_t inputs[] = {400000, 400000, 400000, 400000, 900000};
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        const gs232b_time_us_t at_us = UINT64_C(1000) + i * UINT64_C(20000);
        const gs232b_sample_t input = sample(inputs[i], at_us, true);
        CHECK(gs232b_feedback_filter_push(&filter, &input, at_us) == GS232B_OK);
    }
    CHECK(filter.count == GS232B_FEEDBACK_FILTER_WINDOW);
    CHECK(gs232b_feedback_filter_read(&filter, 81000, &value) == GS232B_OK);
    CHECK(value.valid && value.adc_uv == 400000 && value.acquired_at_us == 81000);

    const gs232b_sample_t boundary_sample = sample(400000, 0, true);
    CHECK(gs232b_feedback_sample_validate(&boundary_sample,
                                           GS232B_FEEDBACK_SAMPLE_MAX_AGE_US) == GS232B_OK);
    CHECK(gs232b_feedback_sample_validate(&boundary_sample,
                                           GS232B_FEEDBACK_SAMPLE_MAX_AGE_US + 1U) ==
          GS232B_ERROR_NOT_READY);
    CHECK(gs232b_feedback_filter_read(&filter, 181001, &value) == GS232B_ERROR_NOT_READY);
    CHECK(!value.valid);

    const gs232b_sample_t future = sample(410000, 200000, true);
    const gs232b_sample_t invalid = sample(410000, 0, false);
    const gs232b_sample_t overrange =
        sample(GS232B_FEEDBACK_FULL_SCALE_UV + 1U, 200000, true);
    CHECK(gs232b_feedback_filter_push(&filter, &future, 199999) == GS232B_ERROR_NOT_READY);
    CHECK(gs232b_feedback_filter_push(&filter, &invalid, 200000) == GS232B_ERROR_NOT_READY);
    CHECK(gs232b_feedback_filter_push(&filter, &overrange, 200000) ==
          GS232B_ERROR_HARDWARE_FAULT);
    CHECK(filter.count == 0);
    CHECK(gs232b_feedback_sample_validate(&overrange, 200000) ==
          GS232B_ERROR_HARDWARE_FAULT);
    return true;
}

static bool test_two_axis_conversion_and_independent_zero(void)
{
    gs232b_fake_store_t store_fake;
    gs232b_fake_store_init(&store_fake);
    const gs232b_store_t store = gs232b_fake_store_port(&store_fake);
    const gs232b_time_us_t now_us = 50000;
    const gs232b_feedback_reading_t az_zero = reading(100000, now_us);
    const gs232b_feedback_reading_t el_zero = reading(200000, now_us);
    CHECK(gs232b_feedback_zero_save(&store, GS232B_AXIS_AZIMUTH, &az_zero, now_us) ==
          GS232B_OK);
    CHECK(gs232b_feedback_zero_save(&store, GS232B_AXIS_ELEVATION, &el_zero, now_us) ==
          GS232B_OK);

    uint32_t loaded_az = 0;
    uint32_t loaded_el = 0;
    CHECK(gs232b_feedback_zero_load(&store, GS232B_AXIS_AZIMUTH, &loaded_az) == GS232B_OK);
    CHECK(gs232b_feedback_zero_load(&store, GS232B_AXIS_ELEVATION, &loaded_el) == GS232B_OK);
    CHECK(loaded_az == 100000 && loaded_el == 200000);

    int32_t position = -1;
    CHECK(gs232b_position_from_feedback(GS232B_AXIS_AZIMUTH, loaded_az,
                                        GS232B_FEEDBACK_FULL_SCALE_UV, &position) == GS232B_OK);
    CHECK(position == GS232B_AZIMUTH_MAX_MDEG);
    CHECK(gs232b_position_from_feedback(GS232B_AXIS_ELEVATION, loaded_el,
                                        GS232B_FEEDBACK_FULL_SCALE_UV, &position) == GS232B_OK);
    CHECK(position == GS232B_ELEVATION_MAX_MDEG);

    const uint32_t az_mid = loaded_az +
                            (GS232B_FEEDBACK_FULL_SCALE_UV - loaded_az) / 2U;
    const uint32_t el_mid = loaded_el +
                            (GS232B_FEEDBACK_FULL_SCALE_UV - loaded_el) / 2U;
    CHECK(gs232b_position_from_feedback(GS232B_AXIS_AZIMUTH, loaded_az, az_mid,
                                        &position) == GS232B_OK);
    CHECK(position == 225000);
    CHECK(gs232b_position_from_feedback(GS232B_AXIS_ELEVATION, loaded_el, el_mid,
                                        &position) == GS232B_OK);
    CHECK(position == 90000);
    CHECK(gs232b_position_from_feedback(
              GS232B_AXIS_AZIMUTH,
              GS232B_FEEDBACK_FULL_SCALE_UV - GS232B_FEEDBACK_MIN_SPAN_UV + 1U,
              GS232B_FEEDBACK_FULL_SCALE_UV, &position) == GS232B_ERROR_NOT_READY);

    const gs232b_feedback_reading_t az_rezero = reading(300000, now_us);
    CHECK(gs232b_feedback_zero_save(&store, GS232B_AXIS_AZIMUTH, &az_rezero, now_us) ==
          GS232B_OK);
    CHECK(gs232b_feedback_zero_load(&store, GS232B_AXIS_AZIMUTH, &loaded_az) == GS232B_OK);
    CHECK(gs232b_feedback_zero_load(&store, GS232B_AXIS_ELEVATION, &loaded_el) == GS232B_OK);
    CHECK(loaded_az == 300000 && loaded_el == 200000);
    return true;
}

static bool test_zero_candidate_guards_preserve_saved_record(void)
{
    gs232b_fake_store_t store_fake;
    gs232b_fake_store_init(&store_fake);
    const gs232b_store_t store = gs232b_fake_store_port(&store_fake);
    const gs232b_time_us_t now_us = 200000;
    const gs232b_feedback_reading_t initial = reading(150000, now_us);
    CHECK(gs232b_feedback_zero_save(&store, GS232B_AXIS_AZIMUTH, &initial, now_us) ==
          GS232B_OK);

    const gs232b_feedback_reading_t stale = reading(180000, 0);
    CHECK(gs232b_feedback_zero_save(&store, GS232B_AXIS_AZIMUTH, &stale, now_us) ==
          GS232B_ERROR_NOT_READY);
    const gs232b_feedback_reading_t invalid = {
        .acquired_at_us = now_us,
        .adc_uv = 180000,
        .valid = false
    };
    CHECK(gs232b_feedback_zero_save(&store, GS232B_AXIS_AZIMUTH, &invalid, now_us) ==
          GS232B_ERROR_NOT_READY);

    const gs232b_feedback_reading_t narrow_span =
        reading(GS232B_FEEDBACK_FULL_SCALE_UV - GS232B_FEEDBACK_MIN_SPAN_UV + 1U, now_us);
    CHECK(gs232b_feedback_zero_validate(GS232B_AXIS_ELEVATION, &narrow_span, now_us) ==
          GS232B_ERROR_HARDWARE_FAULT);
    CHECK(gs232b_feedback_zero_save(&store, GS232B_AXIS_AZIMUTH, &narrow_span, now_us) ==
          GS232B_ERROR_HARDWARE_FAULT);

    const gs232b_feedback_reading_t boundary =
        reading(GS232B_FEEDBACK_FULL_SCALE_UV - GS232B_FEEDBACK_MIN_SPAN_UV, now_us);
    CHECK(gs232b_feedback_zero_validate(GS232B_AXIS_ELEVATION, &boundary, now_us) == GS232B_OK);

    uint32_t loaded = 0;
    CHECK(gs232b_feedback_zero_load(&store, GS232B_AXIS_AZIMUTH, &loaded) == GS232B_OK);
    CHECK(loaded == 150000);
    CHECK(gs232b_feedback_zero_save(&store, GS232B_AXIS_COUNT, &initial, now_us) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    store_fake.records[GS232B_STORE_ZERO_AZ].bytes[9] ^= UINT8_C(0x01);
    CHECK(gs232b_feedback_zero_load(&store, GS232B_AXIS_AZIMUTH, &loaded) ==
          GS232B_ERROR_HARDWARE_FAULT);
    CHECK(loaded == 0);
    return true;
}

int main(void)
{
    if (!test_filter_boundaries_and_freshness() ||
        !test_two_axis_conversion_and_independent_zero() ||
        !test_zero_candidate_guards_preserve_saved_record()) {
        return 1;
    }
    puts("feedback tests passed");
    return 0;
}
