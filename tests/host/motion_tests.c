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

static bool test_readiness_and_atomic_targets(void)
{
    gs232b_motion_config_t config;
    gs232b_motion_config_init(&config);
    gs232b_motion_sim_t sim;
    CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
    const int32_t targets[] = {120000, 120000};
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&sim, &state));
    CHECK(state.axes[0].mode == GS232B_AXIS_DISABLED);
    CHECK(state.axes[1].mode == GS232B_AXIS_DISABLED);
    CHECK(gs232b_motion_manual(&sim.motion, 0, GS232B_MOTION_POSITIVE) ==
          GS232B_ERROR_NOT_READY);
    CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, targets) ==
          GS232B_ERROR_NOT_READY);
    CHECK(stopped(&sim));

    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        gs232b_motion_sim_config(&config);
        config.axes[axis].zero_valid = false;
        CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
        CHECK(gs232b_motion_manual(&sim.motion, (gs232b_axis_id_t)axis,
                                  GS232B_MOTION_POSITIVE) == GS232B_ERROR_NOT_READY);
        CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, targets) ==
              GS232B_ERROR_NOT_READY);
        CHECK(snapshot(&sim, &state));
        CHECK(state.axes[axis].mode == GS232B_AXIS_DISABLED);
        CHECK(!state.axes[0].target_valid && !state.axes[1].target_valid);
        CHECK(stopped(&sim));
        CHECK(gs232b_motion_manual(&sim.motion, (gs232b_axis_id_t)(1 - axis),
                                  GS232B_MOTION_POSITIVE) == GS232B_OK);
        CHECK(advance_ok(&sim, 15));
        CHECK(gs232b_motion_stop(&sim.motion, GS232B_MOTION_ALL_AXES) == GS232B_OK);

        gs232b_motion_sim_config(&config);
        CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
        sim.sample_invalid[axis] = true;
        CHECK(gs232b_motion_manual(&sim.motion, (gs232b_axis_id_t)axis,
                                  GS232B_MOTION_POSITIVE) == GS232B_ERROR_NOT_READY);
        CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, targets) ==
              GS232B_ERROR_NOT_READY);
        CHECK(snapshot(&sim, &state));
        CHECK(!state.axes[axis].position_valid);
        CHECK(!state.axes[0].target_valid && !state.axes[1].target_valid);
        CHECK(stopped(&sim));
    }
    return true;
}

static bool test_parallel_goto_slowdown_and_arrival(void)
{
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    gs232b_motion_sim_t sim;
    CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
    CHECK(gs232b_output_manager_set_w5500_reset(&sim.manager, false) == GS232B_OK);
    const int32_t targets[] = {180000, 120000};
    CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, targets) == GS232B_OK);
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&sim, &state));
    CHECK(state.axes[0].mode == GS232B_AXIS_REVERSAL_WAIT);
    CHECK(state.axes[1].mode == GS232B_AXIS_REVERSAL_WAIT);
    bool parallel = false, full = false, slow = false, el_finished_first = false;
    for (size_t i = 0; i < 2500; ++i) {
        CHECK(gs232b_motion_sim_step(&sim, STEP_US) == GS232B_OK);
        CHECK(snapshot(&sim, &state));
        CHECK((sim.shadow & 0x10U) != 0);
        parallel |= state.axes[0].mode == GS232B_AXIS_GOTO &&
                    state.axes[1].mode == GS232B_AXIS_GOTO;
        full |= sim.pwm_permille == config.azimuth_speed_permille;
        if (sim.pwm_permille == config.azimuth_slow_speed_permille) {
            slow = true;
            CHECK(abs(targets[0] - state.axes[0].position_mdeg) <= 15000);
        }
        el_finished_first |= state.axes[1].mode == GS232B_AXIS_IDLE &&
                             state.axes[0].mode == GS232B_AXIS_GOTO;
        if (state.axes[0].mode == GS232B_AXIS_IDLE && state.axes[1].mode == GS232B_AXIS_IDLE) {
            break;
        }
    }
    CHECK(parallel && full && slow && el_finished_first);
    CHECK(state.axes[0].mode == GS232B_AXIS_IDLE && state.axes[1].mode == GS232B_AXIS_IDLE);
    CHECK(!state.axes[0].target_valid && !state.axes[1].target_valid);
    CHECK(abs(gs232b_motion_sim_position(&sim, 0) - targets[0]) <= config.axes[0].tolerance_mdeg);
    CHECK(abs(gs232b_motion_sim_position(&sim, 1) - targets[1]) <= config.axes[1].tolerance_mdeg);
    CHECK(stopped(&sim));
    sim.position_udeg[0] += INT64_C(3000000);
    CHECK(advance_ok(&sim, 20));
    CHECK(stopped(&sim));
    return true;
}

static bool test_manual_reversal_and_independent_stop(void)
{
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    gs232b_motion_sim_t sim;
    CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
    CHECK(gs232b_motion_manual(&sim.motion, 0, GS232B_MOTION_POSITIVE) == GS232B_OK);
    CHECK(gs232b_motion_manual(&sim.motion, 1, GS232B_MOTION_NEGATIVE) == GS232B_OK);
    CHECK(advance_ok(&sim, 25));
    CHECK((sim.shadow & 0x0fU) == 0x06);
    CHECK(gs232b_motion_manual(&sim.motion, 0, GS232B_MOTION_NEGATIVE) == GS232B_OK);
    const int32_t elevation = gs232b_motion_sim_position(&sim, 1);
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&sim, &state));
    CHECK(state.axes[0].mode == GS232B_AXIS_REVERSAL_WAIT);
    CHECK(state.axes[1].mode == GS232B_AXIS_MANUAL);
    CHECK((sim.shadow & 0x0fU) == 0x02 && sim.pwm_permille == 0);
    CHECK(gs232b_motion_sim_step(&sim, WAIT_US / 2) == GS232B_OK);
    CHECK(gs232b_motion_manual(&sim.motion, 0, GS232B_MOTION_NEGATIVE) == GS232B_OK);
    CHECK(gs232b_motion_sim_step(&sim, WAIT_US / 2 - 1) == GS232B_OK);
    CHECK((sim.shadow & 0x0fU) == 0x02);
    CHECK(gs232b_motion_sim_step(&sim, 1) == GS232B_OK);
    CHECK((sim.shadow & 0x0fU) == 0x0a);
    CHECK(gs232b_motion_sim_position(&sim, 1) < elevation);
    CHECK(advance_ok(&sim, 20));
    CHECK(gs232b_motion_manual(&sim.motion, 0, GS232B_MOTION_STOP) == GS232B_OK);
    CHECK((sim.shadow & 0x0fU) == 0x02);
    CHECK(gs232b_motion_manual(&sim.motion, 0, GS232B_MOTION_POSITIVE) == GS232B_OK);
    CHECK(snapshot(&sim, &state));
    CHECK(state.axes[0].mode == GS232B_AXIS_REVERSAL_WAIT);
    CHECK(gs232b_motion_stop(&sim.motion, GS232B_MOTION_ALL_AXES) == GS232B_OK);
    CHECK(advance_ok(&sim, 30));
    CHECK(stopped(&sim));
    return true;
}

static bool test_goto_reversal_replacement_and_speed(void)
{
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    gs232b_motion_sim_t sim;
    CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
    const int32_t first[] = {120000, 150000};
    CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, first) == GS232B_OK);
    CHECK(advance_ok(&sim, 30));
    const int32_t reverse[] = {80000, 0};
    CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_AZ_MASK, reverse) == GS232B_OK);
    CHECK((sim.shadow & 0x0fU) == 0x01);
    CHECK(gs232b_motion_sim_step(&sim, WAIT_US / 2) == GS232B_OK);
    const int32_t replacement[] = {75000, 0};
    CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_AZ_MASK, replacement) == GS232B_OK);
    CHECK(gs232b_motion_sim_step(&sim, WAIT_US / 2) == GS232B_OK);
    CHECK((sim.shadow & 0x0fU) == 0x09);
    gs232b_motion_axis_config_t invalid = config.axes[0];
    CHECK(gs232b_motion_configure_axis(&sim.motion, 0, &invalid) == GS232B_ERROR_BUSY);
    CHECK(gs232b_motion_set_speed(&sim.motion, 200) == GS232B_OK);
    CHECK(sim.pwm_permille == 200);
    CHECK(run_to_idle(&sim));
    CHECK(abs(gs232b_motion_sim_position(&sim, 0) - replacement[0]) <= 500);
    CHECK(abs(gs232b_motion_sim_position(&sim, 1) - first[1]) <= 500);

    const int32_t next[] = {120000, 120000};
    CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, next) == GS232B_OK);
    CHECK(gs232b_motion_stop(&sim.motion, GS232B_MOTION_AZ_MASK) == GS232B_OK);
    CHECK(advance_ok(&sim, 30));
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&sim, &state));
    CHECK(state.axes[0].mode == GS232B_AXIS_IDLE && !state.axes[0].target_valid);
    CHECK(state.axes[1].mode == GS232B_AXIS_GOTO && state.axes[1].target_mdeg == next[1]);
    CHECK(gs232b_motion_stop(&sim.motion, GS232B_MOTION_ALL_AXES) == GS232B_OK);
    CHECK(stopped(&sim));
    return true;
}

static bool test_endpoints_and_argument_guards(void)
{
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    gs232b_motion_sim_t sim;
    for (int endpoint = 0; endpoint < 2; ++endpoint) {
        CHECK(gs232b_motion_sim_init(&sim, &config, 0, WAIT_US) == GS232B_OK);
        const int32_t targets[] = {endpoint ? 450000 : 0, endpoint ? 180000 : 0};
        CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, targets) == GS232B_OK);
        CHECK(run_to_idle(&sim));
        for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
            CHECK(abs(gs232b_motion_sim_position(&sim, (gs232b_axis_id_t)i) - targets[i]) <= 500);
        }
    }
    const int32_t targets[] = {120000, 181000};
    const size_t attempts = sim.output_attempts;
    CHECK(gs232b_motion_goto(&sim.motion, GS232B_MOTION_ALL_AXES, targets) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_motion_goto(&sim.motion, 0, targets) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_motion_goto(&sim.motion, 4, targets) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_motion_manual(&sim.motion, GS232B_AXIS_COUNT, GS232B_MOTION_POSITIVE) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_motion_set_speed(&sim.motion, 0) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_motion_set_speed(&sim.motion, 1001) == GS232B_ERROR_INVALID_ARGUMENT);
    gs232b_motion_t rejected = {0};
    const gs232b_speed_levels_t invalid_levels[] = {
        {.permille = {0, 500, 750, 1000}},
        {.permille = {250, 500, 750, 1001}},
        {.permille = {250, 250, 750, 1000}},
        {.permille = {250, 750, 500, 1000}}
    };
    for (unsigned i = 0; i < sizeof(invalid_levels) / sizeof(invalid_levels[0]); ++i) {
        config.azimuth_speed_levels = invalid_levels[i];
        CHECK(gs232b_motion_init(&rejected, &sim.manager, sim.motion.sampler, &config) ==
            GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(!rejected.initialized);
    }
    gs232b_motion_axis_config_t invalid = config.axes[0];
    invalid.minimum_mdeg = invalid.maximum_mdeg;
    CHECK(gs232b_motion_configure_axis(&sim.motion, 0, &invalid) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(sim.output_attempts == attempts);
    CHECK(gs232b_motion_tick(NULL) == GS232B_ERROR_INVALID_ARGUMENT);
    gs232b_motion_snapshot_t state;
    CHECK(gs232b_motion_snapshot(NULL, &state) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(!state.output.known && !state.axes[0].position_valid);
    return true;
}

int main(void)
{
    if (!test_readiness_and_atomic_targets() ||
        !test_parallel_goto_slowdown_and_arrival() ||
        !test_manual_reversal_and_independent_stop() ||
        !test_goto_reversal_replacement_and_speed() ||
        !test_endpoints_and_argument_guards()) {
        return 1;
    }
    puts("motion tests passed (SIMULATION: ideal motor model, not hardware validation)");
    return 0;
}
