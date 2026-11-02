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

#include "gs_dispatcher_internal.h"

gs_command_t gs232b_gs_command(const gs232b_control_request_t *request)
{
    if (request->kind != GS232B_LINE_GS || request->position != NULL) {
        return GS_COMMAND_UNKNOWN;
    }
    static const char *const names[] = {
        "", "R", "L", "A", "U", "D", "E", "S", "X1", "X2", "X3", "X4",
        "C", "B", "C2", "N", "H", "H2", "H3", "P36", "P45", "Z", "T"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (gs232b_byte_span_equal_ascii_ci(request->line, names[i])) {
            return (gs_command_t)(i + 1U);
        }
    }
    return GS_COMMAND_UNKNOWN;
}

gs232b_error_t gs232b_gs_validate(void *ctx, const gs232b_control_request_t *request)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    if (request->position != NULL) {
        return GS232B_OK;
    }
    const gs_command_t command = gs232b_gs_command(request);
    if (command == GS_COMMAND_UNKNOWN) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if ((command == GS_COMMAND_R || command == GS_COMMAND_L ||
         (command >= GS_COMMAND_X1 && command <= GS_COMMAND_X4)) &&
        !dispatcher->motion->config.axes[GS232B_AXIS_AZIMUTH].enabled) {
        return GS232B_ERROR_NOT_READY;
    }
    if ((command == GS_COMMAND_U || command == GS_COMMAND_D) &&
        !dispatcher->motion->config.axes[GS232B_AXIS_ELEVATION].enabled) {
        return GS232B_ERROR_NOT_READY;
    }
    return GS232B_OK;
}

static gs232b_error_t display_change(gs232b_dispatcher_t *dispatcher,
                                      gs_command_t command)
{
    gs232b_control_snapshot_t state;
    gs232b_error_t error = gs232b_control_snapshot(&dispatcher->control, &state);
    if (error != GS232B_OK) {
        return error;
    }
    const gs232b_display_change_context_t context = {
        .both_axes_stopped = state.activity.output_stopped &&
            !state.activity.axes_active[0] && !state.activity.axes_active[1],
        .sequence_timer_running = state.sequence.state == GS232B_SEQUENCE_RUNNING,
        .calibration_active = state.activity.interaction != GS232B_INTERACTION_NONE
    };
    gs232b_azimuth_display_settings_t next;
    bool changed;
    error = command == GS_COMMAND_Z ?
        gs232b_azimuth_display_toggle_center(&dispatcher->display, &context, &next, &changed) :
        gs232b_azimuth_display_set_mode(&dispatcher->display,
            command == GS_COMMAND_P36 ? GS232B_AZIMUTH_DISPLAY_P36 :
                                       GS232B_AZIMUTH_DISPLAY_P45,
            &context, &next, &changed);
    if (error != GS232B_OK || !changed) {
        return error;
    }
    uint64_t revision;
    error = gs232b_persistence_display_commit(&dispatcher->store,
        dispatcher->display_revision, &next, &revision);
    if (error == GS232B_OK) {
        dispatcher->display = next;
        dispatcher->display_revision = revision;
    }
    return error;
}

gs232b_error_t gs232b_gs_execute(void *ctx, const gs232b_control_request_t *request)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    if (request->position != NULL) {
        const gs232b_position_plan_t *plan = request->position_plan;
        if (plan == NULL) {
            return GS232B_ERROR_INVALID_ARGUMENT;
        }
        gs232b_error_t error = GS232B_OK;
        if (plan->stop_mask != 0) {
            error = gs232b_motion_stop(dispatcher->motion, plan->stop_mask);
        }
        return error != GS232B_OK ? error : gs232b_motion_goto(dispatcher->motion,
            plan->axis_mask, plan->targets_mdeg);
    }
    const gs_command_t command = gs232b_gs_command(request);
    if (command == GS_COMMAND_R || command == GS_COMMAND_L ||
        command == GS_COMMAND_U || command == GS_COMMAND_D) {
        return gs232b_motion_manual(dispatcher->motion,
            command == GS_COMMAND_R || command == GS_COMMAND_L ?
                GS232B_AXIS_AZIMUTH : GS232B_AXIS_ELEVATION,
            command == GS_COMMAND_R || command == GS_COMMAND_U ?
                GS232B_MOTION_POSITIVE : GS232B_MOTION_NEGATIVE);
    }
    if (command >= GS_COMMAND_X1 && command <= GS_COMMAND_X4) {
        return gs232b_motion_set_speed_level(dispatcher->motion,
            (uint8_t)(command - GS_COMMAND_X1 + 1));
    }
    if (command == GS_COMMAND_P36 || command == GS_COMMAND_P45 || command == GS_COMMAND_Z) {
        return display_change(dispatcher, command);
    }
    return request->access == GS232B_REQUEST_QUERY ? GS232B_OK :
                                                   GS232B_ERROR_INVALID_ARGUMENT;
}

gs232b_error_t gs232b_gs_query(gs232b_dispatcher_t *dispatcher,
    gs_command_t command, gs232b_response_t *response)
{
    *response = (gs232b_response_t){.kind = GS232B_RESPONSE_SUCCESS};
    switch (command) {
    case GS_COMMAND_EMPTY:
        return GS232B_OK;
    case GS_COMMAND_N:
        response->kind = GS232B_RESPONSE_SEQUENCE;
        response->data.sequence.current_point = dispatcher->control.sequence_state.current_point;
        response->data.sequence.point_count = dispatcher->control.sequence_state.point_count;
        return GS232B_OK;
    case GS_COMMAND_H:
        response->kind = GS232B_RESPONSE_HELP_AZIMUTH;
        return GS232B_OK;
    case GS_COMMAND_H2:
        response->kind = GS232B_RESPONSE_HELP_ELEVATION;
        return GS232B_OK;
    case GS_COMMAND_H3:
        response->kind = GS232B_RESPONSE_HELP_DISPLAY;
        response->data.display.mode_450 = dispatcher->display.mode == GS232B_AZIMUTH_DISPLAY_P45;
        response->data.display.south_center =
            dispatcher->display.center == GS232B_AZIMUTH_CENTER_SOUTH;
        return GS232B_OK;
    case GS_COMMAND_C:
    case GS_COMMAND_B:
    case GS_COMMAND_C2:
        break;
    default:
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_motion_snapshot_t state;
    gs232b_error_t error = gs232b_motion_snapshot(dispatcher->motion, &state);
    if (error != GS232B_OK) {
        return error;
    }
    response->kind = command == GS_COMMAND_C ? GS232B_RESPONSE_AZIMUTH :
        command == GS_COMMAND_B ? GS232B_RESPONSE_ELEVATION : GS232B_RESPONSE_BOTH_ANGLES;
    const bool elevation_enabled = dispatcher->motion->config.axes[GS232B_AXIS_ELEVATION].enabled;
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        if ((axis == GS232B_AXIS_AZIMUTH && command == GS_COMMAND_B) ||
            (axis == GS232B_AXIS_ELEVATION && (command == GS_COMMAND_C || !elevation_enabled))) {
            continue;
        }
        const gs232b_axis_snapshot_t *position = &state.axes[axis];
        error = gs232b_feedback_sample_validate(&position->sample, state.captured_at_us);
        if (error != GS232B_OK || !position->position_valid || position->sample_error != GS232B_OK) {
            return error != GS232B_OK ? error : GS232B_ERROR_NOT_READY;
        }
    }
    if (command != GS_COMMAND_B) {
        error = gs232b_azimuth_position_to_display(&dispatcher->display,
            state.axes[GS232B_AXIS_AZIMUTH].position_mdeg, &response->data.angles.azimuth_degrees);
    }
    if (error == GS232B_OK && command != GS_COMMAND_C) {
        error = gs232b_elevation_position_to_display(elevation_enabled,
            state.axes[GS232B_AXIS_ELEVATION].position_mdeg, &response->data.angles.elevation_degrees);
    }
    return error;
}
