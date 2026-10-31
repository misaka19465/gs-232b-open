#include "motion_internal.h"

#include <string.h>

static bool axis_valid(gs232b_axis_id_t axis)
{
    return axis >= GS232B_AXIS_AZIMUTH && axis < GS232B_AXIS_COUNT;
}

static bool mask_valid(uint8_t mask)
{
    return mask != 0 && (mask & (uint8_t)~GS232B_MOTION_ALL_AXES) == 0;
}

bool gs232b_motion_valid(const gs232b_motion_t *motion)
{
    return motion != NULL && motion->initialized && motion->manager != NULL &&
           motion->sampler.read != NULL;
}

bool gs232b_motion_active(const gs232b_motion_t *motion)
{
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        if (motion->axes[i].intent == GS232B_AXIS_MANUAL ||
            motion->axes[i].intent == GS232B_AXIS_GOTO) {
            return true;
        }
    }
    return false;
}

static bool config_valid(gs232b_axis_id_t axis,
                          const gs232b_motion_axis_config_t *config)
{
    int32_t minimum, maximum;
    if (config == NULL ||
        gs232b_axis_mechanical_limits(axis, &minimum, &maximum) != GS232B_OK) {
        return false;
    }
    return config->minimum_mdeg >= minimum && config->maximum_mdeg <= maximum &&
           config->minimum_mdeg < config->maximum_mdeg &&
           config->tolerance_mdeg > 0 &&
           config->tolerance_mdeg < config->maximum_mdeg - config->minimum_mdeg &&
           config->slowdown_mdeg >= config->tolerance_mdeg &&
           config->slowdown_mdeg <= maximum && config->progress_mdeg > 0 &&
           config->reverse_mdeg >= config->progress_mdeg &&
           config->reverse_mdeg <= maximum && config->no_progress_timeout_us != 0 &&
           config->run_timeout_us != 0 &&
           (!config->zero_valid ||
            (config->zero_uv <= GS232B_FEEDBACK_FULL_SCALE_UV &&
             GS232B_FEEDBACK_FULL_SCALE_UV - config->zero_uv >=
                 GS232B_FEEDBACK_MIN_SPAN_UV));
}

static void set_axis_config(gs232b_motion_t *motion, int axis,
                             const gs232b_motion_axis_config_t *config)
{
    motion->config.axes[axis] = *config;
    memset(&motion->axes[axis], 0, sizeof(motion->axes[axis]));
    motion->axes[axis].intent = GS232B_AXIS_IDLE;
    gs232b_axis_snapshot_t *state = &motion->state.axes[axis];
    memset(state, 0, sizeof(*state));
    state->axis = (gs232b_axis_id_t)axis;
    state->minimum_mdeg = config->minimum_mdeg;
    state->maximum_mdeg = config->maximum_mdeg;
    state->limits_valid = true;
    state->zero_valid = config->zero_valid;
    state->sample_error = GS232B_ERROR_NOT_READY;
}

void gs232b_motion_config_init(gs232b_motion_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->azimuth_speed_permille = 800;
    config->azimuth_slow_speed_permille = 300;
    gs232b_speed_levels_init(&config->azimuth_speed_levels);
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        gs232b_motion_axis_config_t *axis = &config->axes[i];
        gs232b_axis_mechanical_limits((gs232b_axis_id_t)i,
                                      &axis->minimum_mdeg, &axis->maximum_mdeg);
        axis->tolerance_mdeg = 500;
        axis->slowdown_mdeg = 15000;
        axis->progress_mdeg = 100;
        axis->reverse_mdeg = 1000;
        axis->no_progress_timeout_us = UINT64_C(5000000);
        axis->run_timeout_us = UINT64_C(300000000);
    }
}

gs232b_error_t gs232b_motion_init(gs232b_motion_t *motion,
                                 gs232b_output_manager_t *manager,
                                 gs232b_sampler_t sampler,
                                 const gs232b_motion_config_t *config)
{
    gs232b_output_manager_snapshot_t output;
    if (motion == NULL || config == NULL || sampler.read == NULL ||
        gs232b_output_manager_snapshot(manager, &output) != GS232B_OK ||
        !config_valid(GS232B_AXIS_AZIMUTH, &config->axes[0]) ||
        !config_valid(GS232B_AXIS_ELEVATION, &config->axes[1]) ||
        config->azimuth_speed_permille == 0 || config->azimuth_speed_permille > 1000 ||
        config->azimuth_slow_speed_permille == 0 ||
        config->azimuth_slow_speed_permille > 1000 ||
        !gs232b_speed_levels_valid(&config->azimuth_speed_levels)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(motion, 0, sizeof(*motion));
    motion->manager = manager;
    motion->sampler = sampler;
    motion->config = *config;
    motion->initialized = true;
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        set_axis_config(motion, i, &config->axes[i]);
    }
    const gs232b_error_t error = gs232b_output_manager_stop_all(manager);
    if (error != GS232B_OK) {
        return gs232b_motion_latch_fault(motion, GS232B_FAULT_OUTPUT, error, 0);
    }
    gs232b_motion_sync_output(motion);
    return gs232b_motion_tick(motion);
}

gs232b_error_t gs232b_motion_configure_axis(gs232b_motion_t *motion,
                                           gs232b_axis_id_t axis,
                                           const gs232b_motion_axis_config_t *config)
{
    if (!gs232b_motion_valid(motion) || !config_valid(axis, config)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_motion_sync_output(motion);
    const gs232b_output_snapshot_t *output = &motion->state.output;
    if (gs232b_motion_active(motion) || !output->known ||
        output->last_successful.azimuth != GS232B_AZ_STOP ||
        output->last_successful.elevation != GS232B_EL_STOP) {
        return GS232B_ERROR_BUSY;
    }
    set_axis_config(motion, axis, config);
    gs232b_motion_sync_output(motion);
    return GS232B_OK;
}

gs232b_error_t gs232b_motion_save_zero(gs232b_motion_t *motion,
                                      const gs232b_store_t *store,
                                      gs232b_axis_id_t axis)
{
    if (!gs232b_motion_valid(motion) || !axis_valid(axis) || store == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_motion_sync_output(motion);
    const gs232b_output_snapshot_t *output = &motion->state.output;
    if (gs232b_motion_active(motion) || !output->known ||
        output->last_successful.azimuth != GS232B_AZ_STOP ||
        output->last_successful.elevation != GS232B_EL_STOP ||
        output->last_successful.speed_permille != 0) {
        return GS232B_ERROR_BUSY;
    }
    if (!motion->config.axes[axis].enabled) {
        return GS232B_ERROR_NOT_READY;
    }
    gs232b_sample_t sample;
    gs232b_error_t error = gs232b_sampler_read(&motion->sampler, axis, &sample);
    const gs232b_time_us_t now_us = motion->manager->clock.now_us(
        motion->manager->clock.ctx);
    if (error == GS232B_OK) {
        error = gs232b_feedback_sample_validate(&sample, now_us);
    }
    if (error == GS232B_OK && motion->axes[axis].sample_seen &&
        sample.acquired_at_us < motion->axes[axis].latest_sample_at_us) {
        error = GS232B_ERROR_NOT_READY;
    }
    if (error != GS232B_OK) {
        return error;
    }
    const gs232b_feedback_reading_t reading = {
        .acquired_at_us = sample.acquired_at_us,
        .adc_uv = sample.adc_uv,
        .valid = true
    };
    error = gs232b_feedback_zero_save(store, axis, &reading, now_us);
    if (error == GS232B_OK) {
        gs232b_motion_axis_config_t next = motion->config.axes[axis];
        next.zero_uv = reading.adc_uv;
        next.zero_valid = true;
        /* NOTE: 停机、轴配置和跨度已验证；存储回调不得重入，因此提交后
         * 直接应用，不引入可能失败的第二次配置操作或回滚写入。 */
        set_axis_config(motion, axis, &next);
        gs232b_motion_sync_output(motion);
    }
    return error;
}

static gs232b_error_t prepare_motion(gs232b_motion_t *motion)
{
    if (motion->state.fault.code != GS232B_FAULT_NONE) {
        return GS232B_ERROR_HARDWARE_FAULT;
    }
    return gs232b_motion_tick(motion);
}

gs232b_error_t gs232b_motion_manual(gs232b_motion_t *motion,
                                   gs232b_axis_id_t axis,
                                   gs232b_motion_direction_t direction)
{
    if (!gs232b_motion_valid(motion) || !axis_valid(axis) ||
        direction < GS232B_MOTION_NEGATIVE || direction > GS232B_MOTION_POSITIVE) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (direction == GS232B_MOTION_STOP) {
        return gs232b_motion_stop(motion, (uint8_t)(1U << axis));
    }
    gs232b_error_t error = prepare_motion(motion);
    if (error != GS232B_OK) {
        return error;
    }
    if (!gs232b_motion_axis_ready(motion, axis) ||
        !motion->state.axes[axis].position_valid) {
        return GS232B_ERROR_NOT_READY;
    }
    motion->axes[axis].intent = GS232B_AXIS_MANUAL;
    motion->axes[axis].manual_direction = direction;
    motion->state.axes[axis].target_valid = false;
    motion->state.axes[axis].target_mdeg = 0;
    return gs232b_motion_drive(motion);
}

gs232b_error_t gs232b_motion_goto(gs232b_motion_t *motion, uint8_t axis_mask,
                                 const int32_t targets_mdeg[GS232B_AXIS_COUNT])
{
    if (!gs232b_motion_valid(motion) || !mask_valid(axis_mask) || targets_mdeg == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        if ((axis_mask & (1U << i)) != 0 &&
            (targets_mdeg[i] < motion->state.axes[i].minimum_mdeg ||
             targets_mdeg[i] > motion->state.axes[i].maximum_mdeg)) {
            return GS232B_ERROR_INVALID_ARGUMENT;
        }
    }
    gs232b_error_t error = prepare_motion(motion);
    if (error != GS232B_OK) {
        return error;
    }
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        if ((axis_mask & (1U << i)) != 0 &&
            (!gs232b_motion_axis_ready(motion, i) ||
             !motion->state.axes[i].position_valid)) {
            return GS232B_ERROR_NOT_READY;
        }
    }
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        if ((axis_mask & (1U << i)) != 0) {
            motion->axes[i].intent = GS232B_AXIS_GOTO;
            motion->state.axes[i].target_mdeg = targets_mdeg[i];
            motion->state.axes[i].target_valid = true;
        }
    }
    return gs232b_motion_drive(motion);
}

gs232b_error_t gs232b_motion_set_speed(gs232b_motion_t *motion, uint16_t speed_permille)
{
    if (!gs232b_motion_valid(motion) || speed_permille == 0 || speed_permille > 1000) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const gs232b_error_t error = prepare_motion(motion);
    if (error != GS232B_OK) {
        return error;
    }
    motion->config.azimuth_speed_permille = speed_permille;
    return gs232b_motion_drive(motion);
}

gs232b_error_t gs232b_motion_set_speed_level(gs232b_motion_t *motion, uint8_t level)
{
    if (!gs232b_motion_valid(motion)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    uint16_t permille;
    const gs232b_error_t error = gs232b_speed_level_permille(
        &motion->config.azimuth_speed_levels, level, &permille);
    return error != GS232B_OK ? error : gs232b_motion_set_speed(motion, permille);
}

gs232b_error_t gs232b_motion_stop(gs232b_motion_t *motion, uint8_t axis_mask)
{
    if (!gs232b_motion_valid(motion) || !mask_valid(axis_mask)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        if ((axis_mask & (1U << i)) != 0) {
            gs232b_motion_cancel_axis(motion, i);
        }
    }
    if (axis_mask != GS232B_MOTION_ALL_AXES &&
        motion->state.fault.code == GS232B_FAULT_NONE) {
        /* NOTE: 单轴停机仍先检查另一轴，不能绕过其反馈和运行期限。 */
        return gs232b_motion_tick(motion);
    }
    const gs232b_error_t error = gs232b_output_manager_stop_all(motion->manager);
    gs232b_motion_sync_output(motion);
    return error == GS232B_OK ? GS232B_OK :
        gs232b_motion_latch_fault(motion, GS232B_FAULT_OUTPUT, error, 0);
}

gs232b_error_t gs232b_motion_abort(gs232b_motion_t *motion,
                                  gs232b_fault_code_t code, gs232b_error_t error)
{
    if (!gs232b_motion_valid(motion) || code <= GS232B_FAULT_NONE ||
        code > GS232B_FAULT_SEQUENCE_TARGET || error == GS232B_OK) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    return gs232b_motion_latch_fault(motion, code, error, 0);
}

gs232b_error_t gs232b_motion_clear_fault(gs232b_motion_t *motion)
{
    if (!gs232b_motion_valid(motion)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_error_t error = gs232b_motion_stop(motion, GS232B_MOTION_ALL_AXES);
    if (error != GS232B_OK) {
        return error;
    }
    gs232b_motion_refresh_feedback(motion);
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        const gs232b_fault_code_t code = gs232b_motion_feedback_fault(motion, i, true);
        if (code != GS232B_FAULT_NONE) {
            error = code == GS232B_FAULT_SAMPLE ? motion->state.axes[i].sample_error :
                                                GS232B_ERROR_HARDWARE_FAULT;
            return gs232b_motion_latch_fault(motion, code, error, (uint8_t)(1U << i));
        }
    }
    memset(&motion->state.fault, 0, sizeof(motion->state.fault));
    gs232b_motion_sync_output(motion);
    return GS232B_OK;
}

gs232b_error_t gs232b_motion_snapshot(const gs232b_motion_t *motion,
                                     gs232b_motion_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!gs232b_motion_valid(motion)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *snapshot = motion->state;
    gs232b_output_manager_snapshot_t output;
    gs232b_output_manager_snapshot(motion->manager, &output);
    snapshot->output = output.output;
    snapshot->captured_at_us = motion->manager->clock.now_us(motion->manager->clock.ctx);
    return GS232B_OK;
}
