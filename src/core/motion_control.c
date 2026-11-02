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

#include "motion_internal.h"

static int8_t applied_direction(const gs232b_output_frame_t *frame, int axis)
{
    const int direction = axis == GS232B_AXIS_AZIMUTH ? (int)frame->azimuth :
                                                     (int)frame->elevation;
    return direction == 0 ? 0 : (direction == 1 ? 1 : -1);
}

void gs232b_motion_cancel_axis(gs232b_motion_t *motion, int axis)
{
    motion->axes[axis].intent = GS232B_AXIS_IDLE;
    motion->axes[axis].manual_direction = GS232B_MOTION_STOP;
    motion->axes[axis].watched_direction = 0;
    motion->state.axes[axis].target_mdeg = 0;
    motion->state.axes[axis].target_valid = false;
}

void gs232b_motion_sync_output(gs232b_motion_t *motion)
{
    gs232b_output_manager_snapshot_t output;
    gs232b_output_manager_snapshot(motion->manager, &output);
    motion->state.output = output.output;
    const gs232b_time_us_t now_us = motion->manager->clock.now_us(motion->manager->clock.ctx);
    motion->state.captured_at_us = now_us;
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        gs232b_motion_axis_t *axis = &motion->axes[i];
        gs232b_axis_snapshot_t *state = &motion->state.axes[i];
        const int8_t direction = output.output.known ?
            applied_direction(&output.output.last_successful, i) : 0;
        if (axis->watched_direction != direction) {
            axis->watched_direction = direction;
            axis->run_started_at_us = now_us;
            axis->progress_at_us = now_us;
            /* NOTE: 换向后重新以原始位置建窗，防止旧方向的滤波尾迹被误判
             * 为新方向反接；方向与无进展检测仅统计实际使能的区间。 */
            if (direction != 0 && axis->raw_position_valid) {
                gs232b_feedback_filter_init(&axis->filter);
                gs232b_feedback_filter_push(&axis->filter, &state->sample, now_us);
                state->position_mdeg = axis->raw_position_mdeg;
                axis->progress_position_mdeg = (int64_t)direction * state->position_mdeg;
                axis->furthest_position_mdeg = axis->progress_position_mdeg;
            }
        }
        if (!motion->config.axes[i].enabled) {
            state->mode = GS232B_AXIS_DISABLED;
        } else if (motion->state.fault.code != GS232B_FAULT_NONE) {
            state->mode = GS232B_AXIS_FAULT;
        } else if (!gs232b_motion_axis_ready(motion, i)) {
            state->mode = GS232B_AXIS_DISABLED;
        } else {
            state->mode = output.reversal_waiting[i] ? GS232B_AXIS_REVERSAL_WAIT :
                                                     axis->intent;
        }
    }
}

gs232b_error_t gs232b_motion_latch_fault(gs232b_motion_t *motion,
                                        gs232b_fault_code_t code,
                                        gs232b_error_t error, uint8_t axis_mask)
{
    if (motion->state.fault.code == GS232B_FAULT_NONE) {
        motion->state.fault = (gs232b_fault_snapshot_t){
            .code = code,
            .error = error,
            .axis_mask = axis_mask,
            .occurred_at_us = motion->manager->clock.now_us(motion->manager->clock.ctx)
        };
        for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
            gs232b_motion_cancel_axis(motion, i);
        }
        /* NOTE: 驱动可能部分提交；这里只尝试一次强制全停，失败时保留
         * output.known=false。后续 tick 不重试，由 STOP/CLEAR 显式重试。 */
        gs232b_output_manager_stop_all(motion->manager);
    }
    gs232b_motion_sync_output(motion);
    return error;
}

static int64_t magnitude(int64_t value)
{
    return value < 0 ? -value : value;
}

gs232b_error_t gs232b_motion_drive(gs232b_motion_t *motion)
{
    gs232b_output_manager_snapshot_t output;
    gs232b_output_manager_snapshot(motion->manager, &output);
    if (!output.output.known) {
        const gs232b_error_t error = output.output.last_error == GS232B_OK ?
            GS232B_ERROR_HARDWARE_FAULT : output.output.last_error;
        return gs232b_motion_latch_fault(motion, GS232B_FAULT_OUTPUT, error, 0);
    }
    gs232b_output_frame_t frame = {
        .w5500_reset_asserted = output.requested.w5500_reset_asserted
    };
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        const gs232b_motion_axis_config_t *config = &motion->config.axes[i];
        gs232b_motion_axis_t *axis = &motion->axes[i];
        gs232b_axis_snapshot_t *state = &motion->state.axes[i];
        int direction = 0;
        int64_t distance = 0;
        if (axis->intent == GS232B_AXIS_GOTO) {
            const int64_t error = (int64_t)state->target_mdeg - state->position_mdeg;
            distance = magnitude(error);
            const bool at_boundary = axis->raw_position_mdeg == state->minimum_mdeg ||
                                     axis->raw_position_mdeg == state->maximum_mdeg;
            /* NOTE: 端点到位优先于向外限位保护，避免滤波滞后使合法端点
             * 目标被误判为越限；其他越界由原始位置检查立即拦截。 */
            if (distance <= config->tolerance_mdeg ||
                (at_boundary && magnitude((int64_t)state->target_mdeg -
                    axis->raw_position_mdeg) <= config->tolerance_mdeg)) {
                gs232b_motion_cancel_axis(motion, i);
            } else {
                direction = error > 0 ? 1 : -1;
            }
        } else if (axis->intent == GS232B_AXIS_MANUAL) {
            direction = axis->manual_direction;
        }
        if ((direction < 0 && axis->raw_position_mdeg <= state->minimum_mdeg) ||
            (direction > 0 && axis->raw_position_mdeg >= state->maximum_mdeg)) {
            return gs232b_motion_latch_fault(motion, GS232B_FAULT_LIMIT,
                GS232B_ERROR_HARDWARE_FAULT, (uint8_t)(1U << i));
        }
        if (i == GS232B_AXIS_AZIMUTH) {
            frame.azimuth = direction == 0 ? GS232B_AZ_STOP :
                            (direction > 0 ? GS232B_AZ_CW : GS232B_AZ_CCW);
            if (direction != 0) {
                frame.speed_permille = motion->config.azimuth_speed_permille;
                if (axis->intent == GS232B_AXIS_GOTO && distance <= config->slowdown_mdeg &&
                    frame.speed_permille > motion->config.azimuth_slow_speed_permille) {
                    frame.speed_permille = motion->config.azimuth_slow_speed_permille;
                }
            }
        } else {
            frame.elevation = direction == 0 ? GS232B_EL_STOP :
                              (direction > 0 ? GS232B_EL_UP : GS232B_EL_DOWN);
        }
    }
    const gs232b_output_t port = gs232b_output_manager_port(motion->manager);
    const gs232b_error_t error = gs232b_output_apply(&port, &frame);
    if (error != GS232B_OK) {
        return gs232b_motion_latch_fault(motion, GS232B_FAULT_OUTPUT, error, 0);
    }
    gs232b_motion_sync_output(motion);
    return GS232B_OK;
}

static gs232b_fault_code_t check_progress(gs232b_motion_t *motion, int i,
                                         gs232b_time_us_t now_us)
{
    gs232b_motion_axis_t *axis = &motion->axes[i];
    const gs232b_motion_axis_config_t *config = &motion->config.axes[i];
    if (axis->watched_direction == 0) {
        return GS232B_FAULT_NONE;
    }
    if (now_us < axis->run_started_at_us ||
        now_us - axis->run_started_at_us >= config->run_timeout_us) {
        return GS232B_FAULT_RUN_TIMEOUT;
    }
    const int64_t position = (int64_t)axis->watched_direction *
                            motion->state.axes[i].position_mdeg;
    if (position > axis->furthest_position_mdeg) {
        axis->furthest_position_mdeg = position;
    }
    if (axis->furthest_position_mdeg - position >= config->reverse_mdeg) {
        return GS232B_FAULT_FEEDBACK_DIRECTION;
    }
    if (position - axis->progress_position_mdeg >= config->progress_mdeg) {
        axis->progress_position_mdeg = position;
        axis->progress_at_us = now_us;
    }
    if (now_us < axis->progress_at_us ||
        now_us - axis->progress_at_us >= config->no_progress_timeout_us) {
        return GS232B_FAULT_NO_PROGRESS;
    }
    return GS232B_FAULT_NONE;
}

gs232b_error_t gs232b_motion_tick(gs232b_motion_t *motion)
{
    if (!gs232b_motion_valid(motion)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_motion_sync_output(motion);
    gs232b_motion_refresh_feedback(motion);
    if (motion->state.fault.code != GS232B_FAULT_NONE) {
        return motion->state.fault.error;
    }
    if (!motion->state.output.known) {
        const gs232b_error_t error = motion->state.output.last_error == GS232B_OK ?
            GS232B_ERROR_HARDWARE_FAULT : motion->state.output.last_error;
        return gs232b_motion_latch_fault(motion, GS232B_FAULT_OUTPUT, error, 0);
    }
    const bool active = gs232b_motion_active(motion);
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        const gs232b_fault_code_t code = gs232b_motion_feedback_fault(motion, i, active);
        if (code != GS232B_FAULT_NONE) {
            const gs232b_error_t error = code == GS232B_FAULT_SAMPLE ?
                motion->state.axes[i].sample_error : GS232B_ERROR_HARDWARE_FAULT;
            return gs232b_motion_latch_fault(motion, code, error, (uint8_t)(1U << i));
        }
    }
    const gs232b_time_us_t now_us = motion->manager->clock.now_us(motion->manager->clock.ctx);
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        const gs232b_fault_code_t code = check_progress(motion, i, now_us);
        if (code != GS232B_FAULT_NONE) {
            return gs232b_motion_latch_fault(motion, code,
                code == GS232B_FAULT_FEEDBACK_DIRECTION ? GS232B_ERROR_HARDWARE_FAULT :
                                                        GS232B_ERROR_TIMEOUT,
                (uint8_t)(1U << i));
        }
    }
    return gs232b_motion_drive(motion);
}
