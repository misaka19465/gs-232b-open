#include "gs232b/coordinates.h"

#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static gs232b_azimuth_display_settings_t p45(void)
{
    gs232b_azimuth_display_settings_t settings;
    gs232b_azimuth_display_settings_init(&settings);
    return settings;
}

static gs232b_azimuth_display_settings_t p36(gs232b_azimuth_center_t center)
{
    gs232b_azimuth_display_settings_t settings = {
        .mode = GS232B_AZIMUTH_DISPLAY_P36,
        .center = center
    };
    return settings;
}

static bool test_display_mapping_and_rounding(void)
{
    uint16_t degrees = 0;
    gs232b_azimuth_display_settings_t settings = p45();

    CHECK(gs232b_azimuth_position_to_display(&settings, 0, &degrees) == GS232B_OK);
    CHECK(degrees == 0);
    CHECK(gs232b_azimuth_position_to_display(&settings, 180000, &degrees) == GS232B_OK);
    CHECK(degrees == 180);
    CHECK(gs232b_azimuth_position_to_display(&settings, 360000, &degrees) == GS232B_OK);
    CHECK(degrees == 360);
    CHECK(gs232b_azimuth_position_to_display(&settings, 450000, &degrees) == GS232B_OK);
    CHECK(degrees == 450);
    CHECK(gs232b_azimuth_position_to_display(&settings, 180500, &degrees) == GS232B_OK);
    CHECK(degrees == 181);
    CHECK(gs232b_azimuth_position_to_display(&settings, 179500, &degrees) == GS232B_OK);
    CHECK(degrees == 180);

    settings = p36(GS232B_AZIMUTH_CENTER_SOUTH);
    CHECK(gs232b_azimuth_position_to_display(&settings, 0, &degrees) == GS232B_OK);
    CHECK(degrees == 0);
    CHECK(gs232b_azimuth_position_to_display(&settings, 180000, &degrees) == GS232B_OK);
    CHECK(degrees == 180);
    CHECK(gs232b_azimuth_position_to_display(&settings, 360000, &degrees) == GS232B_OK);
    CHECK(degrees == 0);
    CHECK(gs232b_azimuth_position_to_display(&settings, 359500, &degrees) == GS232B_OK);
    CHECK(degrees == 0);
    CHECK(gs232b_azimuth_position_to_display(&settings, 180500, &degrees) == GS232B_OK);
    CHECK(degrees == 181);

    settings = p36(GS232B_AZIMUTH_CENTER_NORTH);
    CHECK(gs232b_azimuth_position_to_display(&settings, 0, &degrees) == GS232B_OK);
    CHECK(degrees == 180);
    CHECK(gs232b_azimuth_position_to_display(&settings, 180000, &degrees) == GS232B_OK);
    CHECK(degrees == 0);
    CHECK(gs232b_azimuth_position_to_display(&settings, 359500, &degrees) == GS232B_OK);
    CHECK(degrees == 180);
    return true;
}

static bool test_inverse_and_overlap_selection(void)
{
    int32_t target = -1;
    gs232b_azimuth_display_settings_t settings = p45();
    CHECK(gs232b_azimuth_target_to_position(&settings, 0, 300000, &target) == GS232B_OK);
    CHECK(target == 0);
    CHECK(gs232b_azimuth_target_to_position(&settings, 180, 300000, &target) == GS232B_OK);
    CHECK(target == 180000);
    CHECK(gs232b_azimuth_target_to_position(&settings, 360, 300000, &target) == GS232B_OK);
    CHECK(target == 360000);
    CHECK(gs232b_azimuth_target_to_position(&settings, 450, 0, &target) == GS232B_OK);
    CHECK(target == 450000);

    settings = p36(GS232B_AZIMUTH_CENTER_SOUTH);
    CHECK(gs232b_azimuth_target_to_position(&settings, 0, 10, &target) == GS232B_OK);
    CHECK(target == 0);
    CHECK(gs232b_azimuth_target_to_position(&settings, 0, 350000, &target) == GS232B_OK);
    CHECK(target == 360000);
    CHECK(gs232b_azimuth_target_to_position(&settings, 90, 300000, &target) == GS232B_OK);
    CHECK(target == 450000);
    CHECK(gs232b_azimuth_target_to_position(&settings, 90, 270000, &target) == GS232B_OK);
    CHECK(target == 90000);
    CHECK(gs232b_azimuth_target_to_position(&settings, 90, 450000, &target) == GS232B_OK);
    CHECK(target == 450000);

    settings = p36(GS232B_AZIMUTH_CENTER_NORTH);
    CHECK(gs232b_azimuth_target_to_position(&settings, 180, 0, &target) == GS232B_OK);
    CHECK(target == 0);
    CHECK(gs232b_azimuth_target_to_position(&settings, 0, 180000, &target) == GS232B_OK);
    CHECK(target == 180000);
    CHECK(gs232b_azimuth_target_to_position(&settings, 360, 180000, &target) == GS232B_OK);
    CHECK(target == 180000);
    CHECK(gs232b_azimuth_target_to_position(&settings, 361, 0, &target) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_azimuth_target_to_position(&settings, 0, -1, &target) ==
          GS232B_ERROR_NOT_READY);
    return true;
}

static bool test_electrical_independence_and_invalid_feedback(void)
{
    int32_t position = -1;
    const uint32_t zero_uv = 100000;
    const uint32_t full_uv = GS232B_FEEDBACK_FULL_SCALE_UV;
    CHECK(gs232b_position_from_feedback(GS232B_AXIS_AZIMUTH, zero_uv, zero_uv,
                                        &position) == GS232B_OK);
    CHECK(position == 0);
    CHECK(gs232b_position_from_feedback(GS232B_AXIS_AZIMUTH, zero_uv, full_uv,
                                        &position) == GS232B_OK);
    CHECK(position == 450000);
    CHECK(gs232b_position_from_feedback(GS232B_AXIS_ELEVATION, zero_uv, full_uv,
                                        &position) == GS232B_OK);
    CHECK(position == 180000);

    gs232b_azimuth_display_settings_t current = p45();
    gs232b_azimuth_display_settings_t next = {0};
    bool changed = false;
    const gs232b_display_change_context_t stopped = {
        .both_axes_stopped = true,
        .sequence_timer_running = false,
        .calibration_active = false
    };
    int32_t minimum = -1;
    int32_t maximum = -1;
    int32_t position_before_mode_change = 0;
    int32_t position_after_mode_change = 0;
    CHECK(gs232b_axis_mechanical_limits(GS232B_AXIS_AZIMUTH, &minimum, &maximum) == GS232B_OK);
    CHECK(minimum == GS232B_AZIMUTH_MIN_MDEG && maximum == GS232B_AZIMUTH_MAX_MDEG);
    CHECK(gs232b_position_from_feedback(GS232B_AXIS_AZIMUTH, zero_uv,
                                        (zero_uv + full_uv) / 2,
                                        &position_before_mode_change) == GS232B_OK);
    CHECK(gs232b_azimuth_display_set_mode(&current, GS232B_AZIMUTH_DISPLAY_P36,
                                          &stopped, &next, &changed) == GS232B_OK);
    CHECK(changed);
    CHECK(gs232b_position_from_feedback(GS232B_AXIS_AZIMUTH, zero_uv,
                                        (zero_uv + full_uv) / 2,
                                        &position_after_mode_change) == GS232B_OK);
    CHECK(position_after_mode_change == position_before_mode_change);
    CHECK(position_after_mode_change == 225000);
    CHECK(gs232b_axis_mechanical_limits(GS232B_AXIS_AZIMUTH, &minimum, &maximum) == GS232B_OK);
    CHECK(minimum == GS232B_AZIMUTH_MIN_MDEG && maximum == GS232B_AZIMUTH_MAX_MDEG);
    CHECK(gs232b_axis_mechanical_limits(GS232B_AXIS_ELEVATION, &minimum, &maximum) == GS232B_OK);
    CHECK(minimum == GS232B_ELEVATION_MIN_MDEG && maximum == GS232B_ELEVATION_MAX_MDEG);

    CHECK(gs232b_position_from_feedback(GS232B_AXIS_AZIMUTH, zero_uv, full_uv + 1,
                                        &position) == GS232B_ERROR_HARDWARE_FAULT);
    CHECK(position == 0);
    CHECK(gs232b_position_from_feedback(GS232B_AXIS_AZIMUTH, zero_uv, zero_uv - 1,
                                        &position) == GS232B_ERROR_HARDWARE_FAULT);
    CHECK(position == 0);
    CHECK(gs232b_position_from_feedback(GS232B_AXIS_AZIMUTH, full_uv, full_uv,
                                        &position) == GS232B_ERROR_NOT_READY);

    uint16_t degrees = 123;
    CHECK(gs232b_azimuth_position_to_display(&(gs232b_azimuth_display_settings_t){
                                                 .mode = GS232B_AZIMUTH_DISPLAY_P45,
                                                 .center = GS232B_AZIMUTH_CENTER_NORTH
                                             },
                                             450001, &degrees) == GS232B_ERROR_HARDWARE_FAULT);
    CHECK(degrees == 0);
    return true;
}

static bool test_mode_guards_and_disabled_elevation(void)
{
    gs232b_azimuth_display_settings_t current = p45();
    gs232b_azimuth_display_settings_t next = {0};
    bool changed = true;
    const gs232b_display_change_context_t busy = {
        .both_axes_stopped = false,
        .sequence_timer_running = true,
        .calibration_active = true
    };
    const gs232b_display_change_context_t stopped = {
        .both_axes_stopped = true,
        .sequence_timer_running = false,
        .calibration_active = false
    };
    CHECK(gs232b_azimuth_display_set_mode(&current, GS232B_AZIMUTH_DISPLAY_P36,
                                          &busy, &next, &changed) == GS232B_ERROR_BUSY);
    CHECK(!changed);
    CHECK(gs232b_azimuth_display_toggle_center(&current, &busy, &next, &changed) == GS232B_OK);
    CHECK(!changed && next.center == GS232B_AZIMUTH_CENTER_NORTH);
    CHECK(gs232b_azimuth_display_set_mode(&current, GS232B_AZIMUTH_DISPLAY_P36,
                                          &stopped, &next, &changed) == GS232B_OK);
    CHECK(changed && next.mode == GS232B_AZIMUTH_DISPLAY_P36);
    CHECK(gs232b_azimuth_display_toggle_center(&next, &stopped, &current, &changed) ==
          GS232B_OK);
    CHECK(changed && current.center == GS232B_AZIMUTH_CENTER_SOUTH);
    CHECK(gs232b_azimuth_display_set_mode(&current, GS232B_AZIMUTH_DISPLAY_P36,
                                          &busy, &next, &changed) == GS232B_OK);
    CHECK(!changed);
    CHECK(gs232b_azimuth_display_toggle_center(&current, &busy, &next, &changed) ==
          GS232B_ERROR_BUSY);
    CHECK(!changed);

    uint16_t degrees = 123;
    CHECK(gs232b_elevation_position_to_display(false, 999999, &degrees) == GS232B_OK);
    CHECK(degrees == 0);
    CHECK(gs232b_elevation_target_validate(false, 0) == GS232B_OK);
    CHECK(gs232b_elevation_target_validate(false, 1) == GS232B_ERROR_NOT_READY);
    CHECK(gs232b_elevation_target_validate(true, 180) == GS232B_OK);
    CHECK(gs232b_elevation_target_validate(true, 181) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_elevation_action_validate(false, GS232B_ELEVATION_ACTION_STOP) == GS232B_OK);
    CHECK(gs232b_elevation_action_validate(false, GS232B_ELEVATION_ACTION_MOVE) ==
          GS232B_ERROR_NOT_READY);
    CHECK(gs232b_elevation_action_validate(false, GS232B_ELEVATION_ACTION_ZERO_CALIBRATION) ==
          GS232B_ERROR_NOT_READY);
    CHECK(gs232b_elevation_action_validate(false, GS232B_ELEVATION_ACTION_DISPLAY_CALIBRATION) ==
          GS232B_ERROR_NOT_READY);
    CHECK(gs232b_elevation_position_to_display(true, 180000, &degrees) == GS232B_OK);
    CHECK(degrees == 180);
    CHECK(gs232b_elevation_position_to_display(true, 180500, &degrees) ==
          GS232B_ERROR_HARDWARE_FAULT);
    return true;
}

int main(void)
{
    if (!test_display_mapping_and_rounding() ||
        !test_inverse_and_overlap_selection() ||
        !test_electrical_independence_and_invalid_feedback() ||
        !test_mode_guards_and_disabled_elevation()) {
        return 1;
    }
    puts("coordinate tests passed");
    return 0;
}
