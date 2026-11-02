#include "motion_internal.h"

bool gs232b_motion_axis_ready(const gs232b_motion_t *motion, int axis)
{
    return motion->config.axes[axis].enabled &&
           motion->state.axes[axis].zero_valid &&
           motion->state.axes[axis].limits_valid;
}

void gs232b_motion_refresh_feedback(gs232b_motion_t *motion)
{
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        gs232b_axis_snapshot_t *state = &motion->state.axes[i];
        gs232b_motion_axis_t *axis = &motion->axes[i];
        const gs232b_motion_axis_config_t *config = &motion->config.axes[i];
        state->position_valid = false;
        state->position_mdeg = 0;
        axis->raw_position_valid = false;
        if (!config->enabled) {
            continue;
        }

        gs232b_error_t error = gs232b_sampler_read(
            &motion->sampler, (gs232b_axis_id_t)i, &state->sample);
        const gs232b_time_us_t now_us = motion->manager->clock.now_us(
            motion->manager->clock.ctx);
        if (error == GS232B_OK) {
            error = gs232b_feedback_sample_validate(&state->sample, now_us);
        }
        /* NOTE: 时间戳高水位独立于滤波窗口；故障清窗不能使倒退采样重新
         * 变为有效，也不能允许 CLEAR 用同一异常反馈解除锁定。 */
        if (error == GS232B_OK && axis->sample_seen &&
            state->sample.acquired_at_us < axis->latest_sample_at_us) {
            error = GS232B_ERROR_NOT_READY;
        }
        if (error == GS232B_OK && state->zero_valid) {
            error = gs232b_position_from_feedback((gs232b_axis_id_t)i,
                config->zero_uv, state->sample.adc_uv, &axis->raw_position_mdeg);
            axis->raw_position_valid = error == GS232B_OK;
        }
        /* NOTE: 同一个采样时间戳只入窗一次，避免高频命令反复加权旧样本。 */
        if (error == GS232B_OK && (axis->filter.count == 0 ||
            state->sample.acquired_at_us != axis->filter.latest_at_us)) {
            error = gs232b_feedback_filter_push(&axis->filter, &state->sample, now_us);
        }
        gs232b_feedback_reading_t reading;
        if (error == GS232B_OK) {
            error = gs232b_feedback_filter_read(&axis->filter, now_us, &reading);
        }
        if (error == GS232B_OK && state->zero_valid) {
            error = gs232b_position_from_feedback((gs232b_axis_id_t)i,
                config->zero_uv, reading.adc_uv, &state->position_mdeg);
            state->position_valid = error == GS232B_OK;
        }
        state->sample_error = error;
        if (error != GS232B_OK) {
            gs232b_feedback_filter_init(&axis->filter);
        } else {
            axis->sample_seen = true;
            axis->latest_sample_at_us = state->sample.acquired_at_us;
        }
    }
}

gs232b_fault_code_t gs232b_motion_feedback_fault(const gs232b_motion_t *motion,
                                                int axis, bool require_sample)
{
    if (!gs232b_motion_axis_ready(motion, axis)) {
        return GS232B_FAULT_NONE;
    }
    const gs232b_axis_snapshot_t *state = &motion->state.axes[axis];
    const gs232b_motion_axis_t *runtime = &motion->axes[axis];
    if (runtime->raw_position_valid &&
        (runtime->raw_position_mdeg < state->minimum_mdeg ||
         runtime->raw_position_mdeg > state->maximum_mdeg)) {
        return GS232B_FAULT_LIMIT;
    }
    if (!state->position_valid &&
        (require_sample || state->sample_error == GS232B_ERROR_HARDWARE_FAULT)) {
        return GS232B_FAULT_SAMPLE;
    }
    return GS232B_FAULT_NONE;
}
