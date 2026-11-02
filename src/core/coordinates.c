/*
 * GS-232B Open - rotator controller firmware.
 * Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "gs232b/coordinates.h"

#include <stddef.h>

#define GS232B_AZIMUTH_CIRCLE_MDEG INT32_C(360000)
#define GS232B_AZIMUTH_NORTH_OFFSET_MDEG INT32_C(180000)

static bool axis_valid(gs232b_axis_id_t axis)
{
    return axis == GS232B_AXIS_AZIMUTH || axis == GS232B_AXIS_ELEVATION;
}

static bool settings_valid(const gs232b_azimuth_display_settings_t *settings)
{
    return settings != NULL &&
           (settings->mode == GS232B_AZIMUTH_DISPLAY_P36 ||
            settings->mode == GS232B_AZIMUTH_DISPLAY_P45) &&
           (settings->center == GS232B_AZIMUTH_CENTER_NORTH ||
            settings->center == GS232B_AZIMUTH_CENTER_SOUTH);
}

static bool position_in_limits(gs232b_axis_id_t axis, int32_t position_mdeg)
{
    if (axis == GS232B_AXIS_AZIMUTH) {
        return position_mdeg >= GS232B_AZIMUTH_MIN_MDEG &&
               position_mdeg <= GS232B_AZIMUTH_MAX_MDEG;
    }
    if (axis == GS232B_AXIS_ELEVATION) {
        return position_mdeg >= GS232B_ELEVATION_MIN_MDEG &&
               position_mdeg <= GS232B_ELEVATION_MAX_MDEG;
    }
    return false;
}

static uint16_t rounded_degrees(int32_t position_mdeg)
{
    return (uint16_t)((position_mdeg + INT32_C(500)) / INT32_C(1000));
}

static bool change_allowed(const gs232b_display_change_context_t *context)
{
    return context->both_axes_stopped && !context->sequence_timer_running &&
           !context->calibration_active;
}

void gs232b_azimuth_display_settings_init(gs232b_azimuth_display_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    settings->mode = GS232B_AZIMUTH_DISPLAY_P45;
    settings->center = GS232B_AZIMUTH_CENTER_NORTH;
}

gs232b_error_t gs232b_azimuth_display_set_mode(
    const gs232b_azimuth_display_settings_t *current,
    gs232b_azimuth_display_mode_t mode,
    const gs232b_display_change_context_t *context,
    gs232b_azimuth_display_settings_t *next,
    bool *changed)
{
    if (changed == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *changed = false;
    if (!settings_valid(current) || context == NULL || next == NULL ||
        (mode != GS232B_AZIMUTH_DISPLAY_P36 && mode != GS232B_AZIMUTH_DISPLAY_P45)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }

    *next = *current;
    if (mode == current->mode) {
        return GS232B_OK;
    }
    if (!change_allowed(context)) {
        return GS232B_ERROR_BUSY;
    }

    next->mode = mode;
    *changed = true;
    return GS232B_OK;
}

gs232b_error_t gs232b_azimuth_display_toggle_center(
    const gs232b_azimuth_display_settings_t *current,
    const gs232b_display_change_context_t *context,
    gs232b_azimuth_display_settings_t *next,
    bool *changed)
{
    if (changed == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *changed = false;
    if (!settings_valid(current) || context == NULL || next == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }

    *next = *current;
    if (current->mode == GS232B_AZIMUTH_DISPLAY_P45) {
        return GS232B_OK;
    }
    if (!change_allowed(context)) {
        return GS232B_ERROR_BUSY;
    }

    next->center = current->center == GS232B_AZIMUTH_CENTER_NORTH
                       ? GS232B_AZIMUTH_CENTER_SOUTH
                       : GS232B_AZIMUTH_CENTER_NORTH;
    *changed = true;
    return GS232B_OK;
}

gs232b_error_t gs232b_axis_mechanical_limits(gs232b_axis_id_t axis,
                                             int32_t *minimum_mdeg,
                                             int32_t *maximum_mdeg)
{
    if (minimum_mdeg == NULL || maximum_mdeg == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *minimum_mdeg = 0;
    *maximum_mdeg = 0;
    if (!axis_valid(axis)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }

    if (axis == GS232B_AXIS_AZIMUTH) {
        *minimum_mdeg = GS232B_AZIMUTH_MIN_MDEG;
        *maximum_mdeg = GS232B_AZIMUTH_MAX_MDEG;
    } else {
        *minimum_mdeg = GS232B_ELEVATION_MIN_MDEG;
        *maximum_mdeg = GS232B_ELEVATION_MAX_MDEG;
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_position_from_feedback(gs232b_axis_id_t axis,
                                             uint32_t zero_uv,
                                             uint32_t adc_uv,
                                             int32_t *position_mdeg)
{
    if (position_mdeg == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *position_mdeg = 0;
    if (!axis_valid(axis)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (zero_uv >= GS232B_FEEDBACK_FULL_SCALE_UV ||
        GS232B_FEEDBACK_FULL_SCALE_UV - zero_uv < GS232B_FEEDBACK_MIN_SPAN_UV) {
        return GS232B_ERROR_NOT_READY;
    }
    if (adc_uv < zero_uv || adc_uv > GS232B_FEEDBACK_FULL_SCALE_UV) {
        return GS232B_ERROR_HARDWARE_FAULT;
    }

    const int32_t maximum_mdeg = axis == GS232B_AXIS_AZIMUTH
                                     ? GS232B_AZIMUTH_MAX_MDEG
                                     : GS232B_ELEVATION_MAX_MDEG;
    const uint64_t span_uv = GS232B_FEEDBACK_FULL_SCALE_UV - zero_uv;
    const uint64_t delta_uv = adc_uv - zero_uv;
    const uint64_t numerator = delta_uv * (uint32_t)maximum_mdeg;
    *position_mdeg = (int32_t)((numerator + span_uv / 2) / span_uv);
    return GS232B_OK;
}

gs232b_error_t gs232b_azimuth_position_to_display(
    const gs232b_azimuth_display_settings_t *settings,
    int32_t position_mdeg,
    uint16_t *degrees)
{
    if (degrees == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *degrees = 0;
    if (!settings_valid(settings)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!position_in_limits(GS232B_AXIS_AZIMUTH, position_mdeg)) {
        return GS232B_ERROR_HARDWARE_FAULT;
    }

    int32_t display_mdeg = position_mdeg;
    if (settings->mode == GS232B_AZIMUTH_DISPLAY_P36) {
        if (settings->center == GS232B_AZIMUTH_CENTER_NORTH) {
            display_mdeg += GS232B_AZIMUTH_NORTH_OFFSET_MDEG;
        }
        display_mdeg %= GS232B_AZIMUTH_CIRCLE_MDEG;
        *degrees = (uint16_t)(rounded_degrees(display_mdeg) % 360U);
        return GS232B_OK;
    }

    *degrees = rounded_degrees(display_mdeg);
    return GS232B_OK;
}

gs232b_error_t gs232b_azimuth_target_to_position(
    const gs232b_azimuth_display_settings_t *settings,
    uint16_t display_degrees,
    int32_t reference_position_mdeg,
    int32_t *target_mdeg)
{
    if (target_mdeg == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *target_mdeg = 0;
    if (!settings_valid(settings)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }

    if (settings->mode == GS232B_AZIMUTH_DISPLAY_P45) {
        if (display_degrees > 450U) {
            return GS232B_ERROR_INVALID_ARGUMENT;
        }
        *target_mdeg = (int32_t)display_degrees * INT32_C(1000);
        return GS232B_OK;
    }

    if (display_degrees > 360U) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!position_in_limits(GS232B_AXIS_AZIMUTH, reference_position_mdeg)) {
        return GS232B_ERROR_NOT_READY;
    }

    int32_t base_degrees = (int32_t)(display_degrees % 360U);
    if (settings->center == GS232B_AZIMUTH_CENTER_NORTH) {
        base_degrees = (base_degrees + 180) % 360;
    }
    int32_t best_mdeg = base_degrees * INT32_C(1000);
    int64_t best_distance = (int64_t)reference_position_mdeg - best_mdeg;
    if (best_distance < 0) {
        best_distance = -best_distance;
    }

    const int32_t overlap_mdeg = best_mdeg + GS232B_AZIMUTH_CIRCLE_MDEG;
    if (overlap_mdeg <= GS232B_AZIMUTH_MAX_MDEG) {
        int64_t overlap_distance = (int64_t)reference_position_mdeg - overlap_mdeg;
        if (overlap_distance < 0) {
            overlap_distance = -overlap_distance;
        }
        if (overlap_distance < best_distance) {
            best_mdeg = overlap_mdeg;
        }
    }

    *target_mdeg = best_mdeg;
    return GS232B_OK;
}

gs232b_error_t gs232b_elevation_position_to_display(bool elevation_enabled,
                                                    int32_t position_mdeg,
                                                    uint16_t *degrees)
{
    if (degrees == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *degrees = 0;
    if (!elevation_enabled) {
        return GS232B_OK;
    }
    if (!position_in_limits(GS232B_AXIS_ELEVATION, position_mdeg)) {
        return GS232B_ERROR_HARDWARE_FAULT;
    }

    *degrees = rounded_degrees(position_mdeg);
    return GS232B_OK;
}

gs232b_error_t gs232b_elevation_target_validate(bool elevation_enabled,
                                                uint16_t target_degrees)
{
    if (elevation_enabled) {
        return target_degrees <= 180U ? GS232B_OK : GS232B_ERROR_INVALID_ARGUMENT;
    }
    return target_degrees == 0U ? GS232B_OK : GS232B_ERROR_NOT_READY;
}

gs232b_error_t gs232b_elevation_action_validate(bool elevation_enabled,
                                                gs232b_elevation_action_t action)
{
    if (action < GS232B_ELEVATION_ACTION_MOVE || action > GS232B_ELEVATION_ACTION_STOP) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (elevation_enabled || action == GS232B_ELEVATION_ACTION_STOP) {
        return GS232B_OK;
    }
    return GS232B_ERROR_NOT_READY;
}
