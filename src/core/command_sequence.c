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

#include "command_control_internal.h"

static gs232b_time_us_t interval_us(const gs232b_control_t *control)
{
    return (gs232b_time_us_t)control->sequence.active.interval_s * UINT64_C(1000000);
}

gs232b_error_t gs232b_control_sequence_display_change(gs232b_control_t *control,
    const gs232b_control_request_t *request, bool *changed)
{
    *changed = false;
    if (request->kind != GS232B_LINE_GS || request->position != NULL) {
        return GS232B_OK;
    }
    const bool p36 = gs232b_byte_span_equal_ascii_ci(request->line, "P36");
    const bool p45 = gs232b_byte_span_equal_ascii_ci(request->line, "P45");
    const bool center = gs232b_byte_span_equal_ascii_ci(request->line, "Z");
    if (!p36 && !p45 && !center) {
        return GS232B_OK;
    }
    gs232b_position_context_t position;
    gs232b_control_activity_t activity;
    gs232b_error_t error = control->executor.position_context(control->executor.ctx, &position);
    if (error != GS232B_OK) {
        return error;
    }
    error = gs232b_control_activity(control, &activity);
    if (error != GS232B_OK) {
        return error;
    }
    const gs232b_display_change_context_t context = {
        .both_axes_stopped = activity.output_stopped &&
                            !activity.axes_active[0] && !activity.axes_active[1],
        .sequence_timer_running = control->sequence_state.state == GS232B_SEQUENCE_RUNNING,
        .calibration_active = activity.interaction != GS232B_INTERACTION_NONE
    };
    gs232b_azimuth_display_settings_t next;
    return center ? gs232b_azimuth_display_toggle_center(&position.display,
        &context, &next, changed) : gs232b_azimuth_display_set_mode(&position.display,
        p36 ? GS232B_AZIMUTH_DISPLAY_P36 : GS232B_AZIMUTH_DISPLAY_P45,
        &context, &next, changed);
}

void gs232b_control_sequence_clear(gs232b_control_t *control)
{
    gs232b_sequence_store_clear(&control->sequence);
    control->sequence_state = (gs232b_sequence_snapshot_t){0};
    control->sequence_started_at_us = 0;
    control->sequence_checked_at_us = 0;
}

void gs232b_control_sequence_loaded(gs232b_control_t *control)
{
    control->sequence_state = (gs232b_sequence_snapshot_t){0};
    control->sequence_started_at_us = 0;
    control->sequence_checked_at_us = 0;
    if (control->sequence.active.point_count != 0) {
        control->sequence_state = (gs232b_sequence_snapshot_t){
            .state = GS232B_SEQUENCE_LOADED_WAIT_T,
            .current_point = 1,
            .point_count = control->sequence.active.point_count
        };
    }
}

gs232b_error_t gs232b_control_sequence_start(gs232b_control_t *control)
{
    gs232b_control_activity_t activity = {0};
    const gs232b_error_t error = gs232b_control_activity(control, &activity);
    if (error != GS232B_OK || activity.faulted ||
        activity.interaction != GS232B_INTERACTION_NONE) {
        return error != GS232B_OK ? error : activity.faulted ?
            GS232B_ERROR_HARDWARE_FAULT : GS232B_ERROR_BUSY;
    }
    gs232b_sequence_snapshot_t *state = &control->sequence_state;
    if (state->state == GS232B_SEQUENCE_RUNNING) {
        return GS232B_OK;
    }
    if (state->state != GS232B_SEQUENCE_LOADED_WAIT_T) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const gs232b_time_us_t now_us = control->clock.now_us(control->clock.ctx);
    const gs232b_time_us_t step_us = interval_us(control);
    const gs232b_time_us_t span_us = (state->point_count - 1U) * step_us;
    /* NOTE: 开始前验证全部绝对截止时间，不能到中途才发现时间戳回绕。 */
    if (span_us > UINT64_MAX - now_us) {
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    control->sequence_started_at_us = now_us;
    control->sequence_checked_at_us = now_us;
    state->next_deadline_us = now_us + step_us;
    state->state = GS232B_SEQUENCE_RUNNING;
    return GS232B_OK;
}

static gs232b_error_t fail(gs232b_control_t *control, gs232b_fault_code_t code,
                            gs232b_error_t error)
{
    gs232b_control_sequence_clear(control);
    const gs232b_error_t result = control->executor.fault(control->executor.ctx, code, error);
    gs232b_control_observe_fault(control);
    return result != GS232B_OK ? result : error;
}

gs232b_error_t gs232b_control_sequence_check(gs232b_control_t *control)
{
    const gs232b_sequence_snapshot_t *state = &control->sequence_state;
    if (state->state != GS232B_SEQUENCE_RUNNING) {
        return GS232B_OK;
    }
    const gs232b_time_us_t now_us = control->clock.now_us(control->clock.ctx);
    if (now_us < control->sequence_checked_at_us ||
        (now_us >= state->next_deadline_us &&
         now_us - state->next_deadline_us >= interval_us(control))) {
        /* NOTE: 包括末点严重迟到；先全停，禁止补发已过完整间隔的目标。 */
        return fail(control, GS232B_FAULT_SEQUENCE_LATE, GS232B_ERROR_TIMEOUT);
    }
    control->sequence_checked_at_us = now_us;
    return GS232B_OK;
}

gs232b_error_t gs232b_control_sequence_tick(gs232b_control_t *control)
{
    const gs232b_error_t error = gs232b_control_sequence_check(control);
    gs232b_sequence_snapshot_t *state = &control->sequence_state;
    if (error != GS232B_OK || state->state != GS232B_SEQUENCE_RUNNING ||
        control->sequence_checked_at_us < state->next_deadline_us) {
        return error;
    }
    const gs232b_sequence_data_t *data = &control->sequence.active;
    const bool paired = data->command == 'W';
    const size_t offset = state->current_point * (paired ? 2U : 1U);
    gs232b_position_plan_t plan = {.axis_mask = control->sequence.first.axis_mask};
    plan.targets_mdeg[GS232B_AXIS_AZIMUTH] = (int32_t)data->angles[offset] * INT32_C(1000);
    if (paired) {
        plan.targets_mdeg[GS232B_AXIS_ELEVATION] =
            (int32_t)data->angles[offset + 1U] * INT32_C(1000);
    }
    const gs232b_error_t target_error =
        control->executor.sequence_target(control->executor.ctx, &plan);
    if (target_error != GS232B_OK) {
        return fail(control, GS232B_FAULT_SEQUENCE_TARGET, target_error);
    }
    ++state->current_point;
    if (state->current_point == state->point_count) {
        state->state = GS232B_SEQUENCE_COMPLETED;
        state->next_deadline_us = 0;
    } else {
        /* NOTE: 只从 T 起点和已下发序号计算；实际调度迟延不累积到后续点。 */
        state->next_deadline_us = control->sequence_started_at_us +
                                  state->current_point * interval_us(control);
    }
    return GS232B_OK;
}
