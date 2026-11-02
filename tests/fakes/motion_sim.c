#include "gs232b/motion_sim.h"

#include <string.h>

static gs232b_time_us_t clock_now(void *ctx)
{
    return ((gs232b_motion_sim_t *)ctx)->now_us;
}

static int8_t direction(uint8_t shadow, int axis)
{
    const unsigned bits = axis == GS232B_AXIS_AZIMUTH ? (shadow >> 2) & 3U : shadow & 3U;
    return bits == 0 ? 0 : (bits == 1 ? 1 : -1);
}

static gs232b_error_t apply(void *ctx, uint8_t shadow, uint16_t pwm_permille)
{
    gs232b_motion_sim_t *sim = ctx;
    ++sim->output_attempts;
    const gs232b_time_us_t started_at_us = sim->now_us;
    if (UINT64_MAX - sim->now_us < sim->driver_latency_us) {
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    sim->now_us += sim->driver_latency_us;
    if ((shadow & 0x03U) == 0x03U || (shadow & 0x0cU) == 0x0cU ||
        (shadow & 0xe0U) != 0 || pwm_permille > 1000 ||
        ((shadow & 0x0cU) == 0 && pwm_permille != 0)) {
        sim->unsafe_output = true;
    }
    if (sim->output_failures != 0) {
        --sim->output_failures;
        memset(sim->stop_known, 0, sizeof(sim->stop_known));
        memset(sim->last_direction, 0, sizeof(sim->last_direction));
        return sim->output_error;
    }
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        const int8_t next = direction(shadow, i);
        if (next == 0) {
            if (!sim->stop_known[i]) {
                sim->stop_known[i] = true;
                sim->stopped_at_us[i] = sim->now_us;
            }
        } else {
            if (next != sim->last_direction[i] &&
                (!sim->stop_known[i] || started_at_us < sim->stopped_at_us[i] ||
                 started_at_us - sim->stopped_at_us[i] < sim->reversal_wait_us)) {
                sim->unsafe_output = true;
            }
            sim->last_direction[i] = next;
            sim->stop_known[i] = false;
        }
    }
    sim->shadow = shadow;
    sim->pwm_permille = pwm_permille;
    return GS232B_OK;
}

static gs232b_error_t read_sample(void *ctx, gs232b_axis_id_t axis, gs232b_sample_t *sample)
{
    gs232b_motion_sim_t *sim = ctx;
    if (sim->sample_error[axis] != GS232B_OK) {
        return sim->sample_error[axis];
    }
    if (!sim->sample_frozen[axis]) {
        sim->sample_at_us[axis] = sim->now_us;
    }
    const int64_t maximum_udeg = axis == GS232B_AXIS_AZIMUTH ?
                                INT64_C(450000000) : INT64_C(180000000);
    const int64_t span = GS232B_FEEDBACK_FULL_SCALE_UV - sim->zero_uv[axis];
    int64_t uv = sim->zero_uv[axis] +
        (sim->position_udeg[axis] * span + maximum_udeg / 2) / maximum_udeg;
    if (uv < 0) {
        uv = 0;
    }
    if (uv > UINT32_MAX) {
        uv = UINT32_MAX;
    }
    *sample = (gs232b_sample_t){
        .acquired_at_us = sim->sample_at_us[axis],
        .adc_uv = sim->adc_override[axis] ? sim->adc_uv[axis] : (uint32_t)uv,
        .raw = (uint16_t)(((uint64_t)(uint32_t)uv * 4095U) /
                         GS232B_FEEDBACK_FULL_SCALE_UV),
        .valid = !sim->sample_invalid[axis]
    };
    return GS232B_OK;
}

void gs232b_motion_sim_config(gs232b_motion_config_t *config)
{
    gs232b_motion_config_init(config);
    config->parameters_verified = true;
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        config->axes[i].enabled = true;
        config->axes[i].zero_valid = true;
        config->axes[i].zero_uv = 100000;
    }
}

gs232b_error_t gs232b_motion_sim_init(gs232b_motion_sim_t *sim,
                                      const gs232b_motion_config_t *config,
                                      gs232b_time_us_t start_us,
                                      gs232b_time_us_t reversal_wait_us)
{
    memset(sim, 0, sizeof(*sim));
    sim->now_us = start_us;
    sim->reversal_wait_us = reversal_wait_us;
    sim->output_error = GS232B_ERROR_IO;
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        sim->zero_uv[i] = config->axes[i].zero_uv;
        sim->position_udeg[i] = INT64_C(90000000);
    }
    const gs232b_clock_t clock = {.ctx = sim, .now_us = clock_now};
    const gs232b_output_driver_t driver = {.ctx = sim, .apply = apply};
    const gs232b_sampler_t sampler = {.ctx = sim, .read = read_sample};
    gs232b_error_t error = gs232b_output_manager_init(
        &sim->manager, clock, driver, reversal_wait_us);
    return error == GS232B_OK ? gs232b_motion_init(&sim->motion, &sim->manager,
                                                   sampler, config) : error;
}

gs232b_error_t gs232b_motion_sim_step(gs232b_motion_sim_t *sim, gs232b_time_us_t delta_us)
{
    if (delta_us > UINT64_MAX - sim->now_us || delta_us > UINT64_C(1000000000)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        if (sim->stalled[i]) {
            continue;
        }
        const int64_t rate = i == GS232B_AXIS_AZIMUTH ?
            INT64_C(10000000) * sim->pwm_permille / 1000 : INT64_C(6000000);
        const int sign = direction(sim->shadow, i) * (sim->reversed[i] ? -1 : 1);
        sim->position_udeg[i] += sign * rate * (int64_t)delta_us / INT64_C(1000000);
    }
    sim->now_us += delta_us;
    return gs232b_motion_tick(&sim->motion);
}

int32_t gs232b_motion_sim_position(const gs232b_motion_sim_t *sim, gs232b_axis_id_t axis)
{
    return (int32_t)(sim->position_udeg[axis] / 1000);
}
