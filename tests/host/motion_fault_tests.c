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

#include "motion_test_support.h"

static bool expect_fault(gs232b_motion_sim_t *sim, gs232b_fault_code_t code, int axis)
{
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(sim, &state));
    CHECK(state.fault.code == code);
    CHECK(state.fault.axis_mask == (axis < 0 ? 0 : (1U << axis)));
    CHECK(state.axes[0].mode == GS232B_AXIS_FAULT && state.axes[1].mode == GS232B_AXIS_FAULT);
    CHECK(!state.axes[0].target_valid && !state.axes[1].target_valid);
    CHECK(state.output.known);
    CHECK(stopped(sim));
    const int32_t targets[] = {
        (state.axes[0].minimum_mdeg + state.axes[0].maximum_mdeg) / 2,
        (state.axes[1].minimum_mdeg + state.axes[1].maximum_mdeg) / 2
    };
    CHECK(gs232b_motion_goto(&sim->motion, GS232B_MOTION_ALL_AXES, targets) ==
          GS232B_ERROR_HARDWARE_FAULT);
    CHECK(gs232b_motion_manual(&sim->motion, 0, GS232B_MOTION_POSITIVE) ==
          GS232B_ERROR_HARDWARE_FAULT);
    CHECK(gs232b_motion_set_speed(&sim->motion, 500) == GS232B_ERROR_HARDWARE_FAULT);
    return true;
}

static bool fault_within(gs232b_motion_sim_t *sim, size_t steps)
{
    for (size_t i = 0; i < steps; ++i) {
        if (gs232b_motion_sim_step(sim, STEP_US) != GS232B_OK) {
            return true;
        }
    }
    CHECK(false);
}

static bool clear_without_resuming(gs232b_motion_sim_t *sim)
{
    CHECK(gs232b_motion_clear_fault(&sim->motion) == GS232B_OK);
    CHECK(advance_ok(sim, 30));
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(sim, &state));
    CHECK(state.fault.code == GS232B_FAULT_NONE);
    CHECK(state.axes[0].mode == GS232B_AXIS_IDLE && state.axes[1].mode == GS232B_AXIS_IDLE);
    CHECK(!state.axes[0].target_valid && !state.axes[1].target_valid);
    CHECK(stopped(sim));
    return true;
}

static bool test_reverse_and_no_progress_on_each_axis(void)
{
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        for (int failure = 0; failure < 2; ++failure) {
            gs232b_motion_config_t config;
            gs232b_motion_sim_config(&config);
            config.axes[axis].no_progress_timeout_us = UINT64_C(100000);
            if (failure == 0) {
                config.axes[axis].no_progress_timeout_us = UINT64_C(5000000);
            }
            gs232b_motion_sim_t sim;
            CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
            sim.reversed[axis] = failure == 0;
            sim.stalled[axis] = failure == 1;
            const int32_t targets[] = {150000, 150000};
            CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, targets) == GS232B_OK);
            CHECK(advance_ok(&sim, (size_t)(WAIT_US / STEP_US)));
            CHECK(fault_within(&sim, 60));
            CHECK(expect_fault(&sim, failure == 0 ? GS232B_FAULT_FEEDBACK_DIRECTION :
                                                   GS232B_FAULT_NO_PROGRESS, axis));
            CHECK(sim.now_us >= WAIT_US + (failure == 1 ? UINT64_C(100000) : 0));
            sim.reversed[axis] = false;
            sim.stalled[axis] = false;
            CHECK(clear_without_resuming(&sim));
            CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, targets) == GS232B_OK);
            CHECK(advance_ok(&sim, 20));
            CHECK((sim.shadow & 0x0fU) == 0x05);
            CHECK(gs232b_motion_stop(&sim.motion, GS232B_MOTION_ALL_AXES) == GS232B_OK);
        }
    }
    return true;
}

static bool test_timeouts_survive_command_replacement(void)
{
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        for (int manual = 0; manual < 2; ++manual) {
            gs232b_motion_config_t config;
            gs232b_motion_sim_config(&config);
            config.axes[axis].run_timeout_us = UINT64_C(300000);
            gs232b_motion_sim_t sim;
            CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
            const uint8_t mask = (uint8_t)(1U << axis);
            int32_t targets[] = {130000, 130000};
            CHECK(gs232b_motion_goto(&sim.motion, mask, targets) == GS232B_OK);
            for (int i = 0; i < 24; ++i) {
                CHECK(gs232b_motion_sim_step(&sim, STEP_US) == GS232B_OK);
                targets[axis] = (i & 1) ? 130000 : 140000;
                CHECK((manual ? gs232b_motion_manual(&sim.motion, (gs232b_axis_id_t)axis,
                                                     GS232B_MOTION_POSITIVE) :
                                gs232b_motion_goto(&sim.motion, mask, targets)) == GS232B_OK);
            }
            CHECK(gs232b_motion_sim_step(&sim, STEP_US) == GS232B_ERROR_TIMEOUT);
            CHECK(expect_fault(&sim, GS232B_FAULT_RUN_TIMEOUT, axis));
            CHECK(sim.now_us == WAIT_US + UINT64_C(300000));
            CHECK(clear_without_resuming(&sim));
        }

        gs232b_motion_config_t config;
        gs232b_motion_sim_config(&config);
        config.axes[axis].no_progress_timeout_us = UINT64_C(200000);
        gs232b_motion_sim_t sim;
        CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
        sim.stalled[axis] = true;
        const int32_t targets[] = {130000, 130000};
        const uint8_t mask = (uint8_t)(1U << axis);
        CHECK(gs232b_motion_goto(&sim.motion, mask, targets) == GS232B_OK);
        for (int i = 0; i < 19; ++i) {
            CHECK(gs232b_motion_sim_step(&sim, STEP_US) == GS232B_OK);
            CHECK(gs232b_motion_goto(&sim.motion, mask, targets) == GS232B_OK);
        }
        CHECK(gs232b_motion_sim_step(&sim, STEP_US) == GS232B_ERROR_TIMEOUT);
        CHECK(expect_fault(&sim, GS232B_FAULT_NO_PROGRESS, axis));
    }
    return true;
}

static bool test_sample_faults_and_failed_clear(void)
{
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        for (int failure = 0; failure < 7; ++failure) {
            gs232b_motion_config_t config;
            gs232b_motion_sim_config(&config);
            gs232b_motion_sim_t sim;
            CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
            const int32_t targets[] = {130000, 130000};
            CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, targets) == GS232B_OK);
            CHECK(advance_ok(&sim, 20));
            gs232b_time_us_t delta = STEP_US;
            switch (failure) {
            case 0: sim.sample_invalid[axis] = true; break;
            case 1:
                sim.sample_frozen[axis] = true;
                delta = GS232B_FEEDBACK_SAMPLE_MAX_AGE_US + 1;
                break;
            case 2:
                sim.sample_frozen[axis] = true;
                sim.sample_at_us[axis] = sim.now_us + UINT64_C(1000000);
                break;
            case 3: sim.sample_error[axis] = GS232B_ERROR_IO; break;
            case 4:
                sim.adc_override[axis] = true;
                sim.adc_uv[axis] = GS232B_FEEDBACK_FULL_SCALE_UV + 1;
                break;
            case 5:
                sim.adc_override[axis] = true;
                sim.adc_uv[axis] = config.axes[axis].zero_uv - 1;
                break;
            default:
                sim.sample_frozen[axis] = true;
                --sim.sample_at_us[axis];
                break;
            }
            CHECK(gs232b_motion_sim_step(&sim, delta) != GS232B_OK);
            CHECK(expect_fault(&sim, GS232B_FAULT_SAMPLE, axis));
            gs232b_motion_snapshot_t before, after;
            CHECK(snapshot(&sim, &before));
            CHECK(!before.axes[axis].position_valid);
            CHECK(gs232b_motion_clear_fault(&sim.motion) != GS232B_OK);
            CHECK(snapshot(&sim, &after));
            CHECK(after.fault.code == before.fault.code);
            CHECK(after.fault.occurred_at_us == before.fault.occurred_at_us);
            CHECK(stopped(&sim));
            sim.sample_invalid[axis] = false;
            sim.sample_frozen[axis] = false;
            sim.sample_error[axis] = GS232B_OK;
            sim.adc_override[axis] = false;
            CHECK(clear_without_resuming(&sim));
        }
    }
    return true;
}

static bool test_limits_both_directions_and_raw_guard(void)
{
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        for (int negative = 0; negative < 2; ++negative) {
            gs232b_motion_config_t config;
            gs232b_motion_sim_config(&config);
            config.axes[axis].minimum_mdeg = 80000;
            config.axes[axis].maximum_mdeg = 100000;
            gs232b_motion_sim_t sim;
            CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
            CHECK(gs232b_motion_manual(&sim.motion, (gs232b_axis_id_t)axis,
                negative ? GS232B_MOTION_NEGATIVE : GS232B_MOTION_POSITIVE) == GS232B_OK);
            CHECK(fault_within(&sim, 120));
            CHECK(expect_fault(&sim, GS232B_FAULT_LIMIT, axis));
            CHECK(gs232b_motion_clear_fault(&sim.motion) == GS232B_ERROR_HARDWARE_FAULT);
            sim.position_udeg[axis] = INT64_C(90000000);
            CHECK(clear_without_resuming(&sim));

            const int32_t boundary = negative ? 80000 : 100000;
            sim.position_udeg[axis] = (int64_t)boundary * 1000;
            CHECK(gs232b_motion_sim_step(&sim, STEP_US) == GS232B_OK);
            CHECK(gs232b_motion_manual(&sim.motion, (gs232b_axis_id_t)axis,
                negative ? GS232B_MOTION_NEGATIVE : GS232B_MOTION_POSITIVE) ==
                  GS232B_ERROR_HARDWARE_FAULT);
            CHECK(expect_fault(&sim, GS232B_FAULT_LIMIT, axis));
            CHECK(clear_without_resuming(&sim));
            CHECK(gs232b_motion_manual(&sim.motion, (gs232b_axis_id_t)axis,
                negative ? GS232B_MOTION_POSITIVE : GS232B_MOTION_NEGATIVE) == GS232B_OK);
            CHECK(advance_ok(&sim, 20));
            CHECK(gs232b_motion_stop(&sim.motion, GS232B_MOTION_ALL_AXES) == GS232B_OK);

            sim.position_udeg[axis] = (int64_t)(negative ? boundary - 100 : boundary + 100) * 1000;
            CHECK(gs232b_motion_sim_step(&sim, STEP_US) == GS232B_ERROR_HARDWARE_FAULT);
            CHECK(expect_fault(&sim, GS232B_FAULT_LIMIT, axis));
        }
    }
    return true;
}

static bool test_output_unknown_and_explicit_recovery(void)
{
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    gs232b_motion_sim_t sim;
    CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
    const int32_t targets[] = {130000, 130000};
    CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, targets) == GS232B_OK);
    CHECK(advance_ok(&sim, 20));
    sim.output_failures = 2;
    CHECK(gs232b_motion_set_speed(&sim.motion, 700) == GS232B_ERROR_IO);
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&sim, &state));
    CHECK(state.fault.code == GS232B_FAULT_OUTPUT && !state.output.known);
    CHECK(!state.axes[0].target_valid && !state.axes[1].target_valid);
    CHECK(state.output.requested.azimuth == GS232B_AZ_STOP);
    CHECK(state.output.requested.elevation == GS232B_EL_STOP);
    const size_t attempts = sim.output_attempts;
    CHECK(gs232b_motion_sim_step(&sim, STEP_US) == GS232B_ERROR_IO);
    CHECK(sim.output_attempts == attempts);
    sim.output_failures = 1;
    CHECK(gs232b_motion_clear_fault(&sim.motion) == GS232B_ERROR_IO);
    CHECK(snapshot(&sim, &state));
    CHECK(state.fault.code == GS232B_FAULT_OUTPUT && !state.output.known);
    CHECK(gs232b_motion_clear_fault(&sim.motion) == GS232B_OK);
    CHECK(stopped(&sim));
    CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, targets) == GS232B_OK);
    CHECK(snapshot(&sim, &state));
    CHECK(state.axes[0].mode == GS232B_AXIS_REVERSAL_WAIT);
    CHECK(state.axes[1].mode == GS232B_AXIS_REVERSAL_WAIT);
    CHECK(advance_ok(&sim, 5));
    CHECK(stopped(&sim));
    CHECK(gs232b_motion_stop(&sim.motion, GS232B_MOTION_ALL_AXES) == GS232B_OK);
    CHECK(advance_ok(&sim, 30));
    CHECK(stopped(&sim));
    return true;
}

static bool test_time_origin_and_idle_peer_fault(void)
{
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    config.axes[0].no_progress_timeout_us = UINT64_C(100000);
    gs232b_motion_sim_t sim;
    CHECK(gs232b_motion_sim_init(&sim, &config, UINT64_MAX - UINT64_C(1000000), WAIT_US) ==
          GS232B_OK);
    sim.stalled[0] = true;
    CHECK(gs232b_motion_manual(&sim.motion, 0, GS232B_MOTION_POSITIVE) == GS232B_OK);
    CHECK(advance_ok(&sim, 14));
    CHECK(gs232b_motion_sim_step(&sim, STEP_US) == GS232B_ERROR_TIMEOUT);
    CHECK(expect_fault(&sim, GS232B_FAULT_NO_PROGRESS, 0));

    gs232b_motion_sim_config(&config);
    CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
    CHECK(gs232b_motion_manual(&sim.motion, 0, GS232B_MOTION_POSITIVE) == GS232B_OK);
    CHECK(advance_ok(&sim, 15));
    sim.sample_invalid[1] = true;
    CHECK(gs232b_motion_sim_step(&sim, STEP_US) == GS232B_ERROR_NOT_READY);
    CHECK(expect_fault(&sim, GS232B_FAULT_SAMPLE, 1));
    return true;
}

static bool test_fault_cancels_pending_reversals(void)
{
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    gs232b_motion_sim_t sim;
    CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
    const int32_t forward[] = {130000, 130000};
    const int32_t backward[] = {60000, 60000};
    CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, forward) == GS232B_OK);
    CHECK(advance_ok(&sim, 20));
    CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, backward) == GS232B_OK);
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&sim, &state));
    CHECK(state.axes[0].mode == GS232B_AXIS_REVERSAL_WAIT);
    CHECK(state.axes[1].mode == GS232B_AXIS_REVERSAL_WAIT);
    sim.sample_invalid[0] = true;
    CHECK(gs232b_motion_sim_step(&sim, STEP_US) == GS232B_ERROR_NOT_READY);
    CHECK(expect_fault(&sim, GS232B_FAULT_SAMPLE, 0));
    sim.sample_invalid[0] = false;
    CHECK(clear_without_resuming(&sim));
    gs232b_output_manager_snapshot_t output;
    CHECK(gs232b_output_manager_snapshot(&sim.manager, &output) == GS232B_OK);
    CHECK(!output.reversal_waiting[0] && !output.reversal_waiting[1]);
    CHECK(output.requested.azimuth == GS232B_AZ_STOP);
    CHECK(output.requested.elevation == GS232B_EL_STOP);
    return true;
}

static bool test_watchdog_starts_after_output_completion(void)
{
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    config.axes[0].no_progress_timeout_us = UINT64_C(100000);
    gs232b_motion_sim_t sim;
    CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
    sim.driver_latency_us = UINT64_C(50000);
    sim.stalled[0] = true;
    const int32_t target[] = {130000, 0};
    CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_AZ_MASK, target) == GS232B_OK);
    CHECK(gs232b_motion_sim_step(&sim, WAIT_US) == GS232B_OK);
    CHECK(sim.now_us == WAIT_US + sim.driver_latency_us);
    const gs232b_time_us_t started_at_us = sim.now_us;
    CHECK(gs232b_motion_sim_step(&sim, UINT64_C(99999)) == GS232B_OK);
    CHECK(gs232b_motion_sim_step(&sim, 1) == GS232B_ERROR_TIMEOUT);
    CHECK(expect_fault(&sim, GS232B_FAULT_NO_PROGRESS, 0));
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&sim, &state));
    CHECK(state.fault.occurred_at_us == started_at_us + UINT64_C(100000));
    return true;
}

int main(void)
{
    if (!test_reverse_and_no_progress_on_each_axis() ||
        !test_timeouts_survive_command_replacement() ||
        !test_sample_faults_and_failed_clear() ||
        !test_limits_both_directions_and_raw_guard() ||
        !test_output_unknown_and_explicit_recovery() ||
        !test_time_origin_and_idle_peer_fault() ||
        !test_fault_cancels_pending_reversals() ||
        !test_watchdog_starts_after_output_completion()) {
        return 1;
    }
    puts("motion fault tests passed (SIMULATION: injected faults, not hardware validation)");
    return 0;
}
