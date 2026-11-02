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

#include "gs_test_support.h"

static bool test_queries_framing_and_actual_feedback(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_feed(&fixture, 0, " c") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
    CHECK(gs_quiet(&fixture));
    CHECK(gs_feed(&fixture, 0, "2  \r\nb\rC\rN\r\r") == GS232B_OK);
    CHECK(gs_drain(&fixture));
    CHECK(gs_expect(&fixture, 0,
        "AZ=090  EL=090\r\nEL=090\r\nAZ=090\r\n=0000=0000\r\n\r"));
    CHECK(gs_quiet(&fixture));
    CHECK(!gs232b_session_key_valid(fixture.dispatcher.control.owner));

    fixture.sim.position_udeg[0] = INT64_C(449600000);
    fixture.sim.position_udeg[1] = INT64_C(179500000);
    CHECK(gs_step(&fixture, 10));
    CHECK(gs_run(&fixture, 1, "C2\r", "AZ=450  EL=180\r\n"));
    fixture.sim.sample_invalid[0] = true;
    CHECK(gs_run(&fixture, 1, "C\r", "?>\r\n"));
    CHECK(gs_run(&fixture, 2, "B\r", "EL=180\r\n"));
    CHECK(gs_run(&fixture, 2, "N\r", "=0000=0000\r\n"));
    CHECK(gs_quiet(&fixture));
    return true;
}

static bool test_manual_axes_stops_and_all_speeds(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_run(&fixture, 0, "r\r", "\r"));
    CHECK(gs_run(&fixture, 0, "u\r", "\r"));
    CHECK(gs_step(&fixture, 15));
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&fixture.sim, &state));
    CHECK(state.output.last_successful.azimuth == GS232B_AZ_CW);
    CHECK(state.output.last_successful.elevation == GS232B_EL_UP);
    static const char *const speeds[] = {"X1\r", "X2\r", "X3\r", "X4\r"};
    for (size_t i = 0; i < 4; ++i) {
        CHECK(gs_run(&fixture, 0, speeds[i], "\r"));
        CHECK(fixture.sim.pwm_permille == (i + 1U) * 250U);
    }
    CHECK(gs_run(&fixture, 1, "A\r", "\r"));
    CHECK(snapshot(&fixture.sim, &state));
    CHECK(state.output.last_successful.azimuth == GS232B_AZ_STOP);
    CHECK(state.output.last_successful.elevation == GS232B_EL_UP);
    CHECK(gs_run(&fixture, 0, "L\r", "\r"));
    CHECK(gs_run(&fixture, 0, "D\r", "\r"));
    CHECK(gs_step(&fixture, 15));
    CHECK(snapshot(&fixture.sim, &state));
    CHECK(state.output.last_successful.azimuth == GS232B_AZ_CCW);
    CHECK(state.output.last_successful.elevation == GS232B_EL_DOWN);
    CHECK(gs_run(&fixture, 2, "E\r", "\r"));
    CHECK(snapshot(&fixture.sim, &state));
    CHECK(state.output.last_successful.azimuth == GS232B_AZ_CCW);
    CHECK(state.output.last_successful.elevation == GS232B_EL_STOP);
    CHECK(gs_run(&fixture, 1, "S\r", "\r"));
    CHECK(stopped(&fixture.sim));
    CHECK(!gs232b_session_key_valid(fixture.dispatcher.control.owner));
    CHECK(gs_quiet(&fixture));
    return true;
}

static bool test_targets_and_slowdown(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_run(&fixture, 0, "X4\r", "\r"));
    CHECK(gs_feed(&fixture, 0, "M100") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
    CHECK(stopped(&fixture.sim));
    CHECK(gs_feed(&fixture, 0, "\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
    CHECK(gs_expect(&fixture, 0, "\r"));
    CHECK(gs_step(&fixture, 15));
    CHECK(fixture.sim.pwm_permille == 300);
    CHECK(gs_run(&fixture, 0, "X1\r", "\r"));
    CHECK(fixture.sim.pwm_permille == 250);
    CHECK(gs_run(&fixture, 0, "X4\r", "\r"));
    CHECK(gs_run(&fixture, 0, "M180\r", "\r"));
    CHECK(fixture.sim.pwm_permille == 1000);
    CHECK(gs_run(&fixture, 0, "W200 120\r", "\r"));
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&fixture.sim, &state));
    CHECK(state.axes[0].target_mdeg == 200000 && state.axes[1].target_mdeg == 120000);
    CHECK(gs_run(&fixture, 2, "S\r", "\r"));
    CHECK(stopped(&fixture.sim));
    CHECK(gs_quiet(&fixture));
    return true;
}

static bool test_firmware_speed_levels_and_slowdown(void)
{
    gs_fixture_t fixture;
    memset(&fixture, 0, sizeof(fixture));
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    const gs232b_speed_levels_t levels = {.permille = {175, 410, 635, 970}};
    config.azimuth_speed_levels = levels;
    CHECK(gs232b_motion_sim_init(&fixture.sim, &config, 0, WAIT_US) == GS232B_OK);
    /* NOTE: 初始化按值复制固件参数，调用方后续修改不影响已活动实例。 */
    config.azimuth_speed_levels.permille[0] = 99;
    gs232b_fake_store_init(&fixture.store);
    CHECK(gs_boot(&fixture));
    CHECK(gs_run(&fixture, 0, "R\r", "\r"));
    CHECK(gs_run(&fixture, 0, "U\r", "\r"));
    CHECK(gs_step(&fixture, 15));
    static const char *const commands[] = {"X1\r", "X2\r", "X3\r", "X4\r"};
    for (unsigned i = 0; i < GS232B_SPEED_LEVEL_COUNT; ++i) {
        CHECK(gs_run(&fixture, 0, commands[i], "\r"));
        CHECK(fixture.sim.pwm_permille == levels.permille[i]);
        gs232b_motion_snapshot_t state;
        CHECK(snapshot(&fixture.sim, &state));
        CHECK(state.output.known && !state.output.speed_voltage_confirmed);
        CHECK(state.output.last_successful.elevation == GS232B_EL_UP);
    }
    const size_t attempts = fixture.sim.output_attempts;
    CHECK(gs232b_motion_set_speed_level(&fixture.sim.motion, 0) ==
        GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_motion_set_speed_level(&fixture.sim.motion, 5) ==
        GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_motion_set_speed_level(&fixture.sim.motion, UINT8_MAX) ==
        GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_motion_set_speed_level(NULL, 1) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(fixture.sim.output_attempts == attempts && fixture.sim.pwm_permille == 970);
    CHECK(fixture.sim.motion.config.azimuth_speed_permille == 970);
    CHECK(gs_run(&fixture, 0, "M100\r", "\r"));
    CHECK(fixture.sim.pwm_permille == config.azimuth_slow_speed_permille);
    CHECK(gs_run(&fixture, 0, "X1\r", "\r"));
    CHECK(fixture.sim.pwm_permille == levels.permille[0]);
    CHECK(gs_run(&fixture, 2, "S\r", "\r"));
    CHECK(stopped(&fixture.sim));
    return true;
}

static bool test_modes_persistence_mapping_and_atomic_failure(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_run(&fixture, 0, "Z\r", "\r"));
    CHECK(fixture.dispatcher.display_revision == 0);
    CHECK(gs_run(&fixture, 0, "P36\r", "\r"));
    CHECK(gs_run(&fixture, 1, "C\r", "AZ=270\r\n"));
    CHECK(gs_run(&fixture, 0, "M000\r", "\r"));
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&fixture.sim, &state));
    CHECK(state.axes[0].target_mdeg == 180000);
    CHECK(gs_feed(&fixture, 0, "P45\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_ERROR_BUSY);
    CHECK(gs_expect(&fixture, 0, "?>\r\n"));
    CHECK(fixture.dispatcher.display_revision == 1);
    CHECK(gs_run(&fixture, 2, "S\r", "\r"));
    CHECK(gs_run(&fixture, 0, "Z\r", "\r"));
    CHECK(gs_run(&fixture, 1, "C\r", "AZ=090\r\n"));
    CHECK(fixture.dispatcher.display_revision == 2);
    CHECK(gs_run(&fixture, 0, "M010 090 100\r", "\r"));
    CHECK(gs_run(&fixture, 0, "P36\r", "\r"));
    CHECK(gs_run(&fixture, 1, "N\r", "=0001=0002\r\n"));
    CHECK(gs232b_fake_fail_next(&fixture.store.failures[GS232B_STORE_DISPLAY]
        [GS232B_FAKE_STORE_INTERRUPT], GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs_feed(&fixture, 0, "P45\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_ERROR_IO);
    CHECK(gs_expect(&fixture, 0, "?>\r\n"));
    CHECK(fixture.dispatcher.display.mode == GS232B_AZIMUTH_DISPLAY_P36);
    CHECK(gs_run(&fixture, 1, "N\r", "=0001=0002\r\n"));
    CHECK(gs_run(&fixture, 0, "P45\r", "\r"));
    CHECK(gs_run(&fixture, 1, "N\r", "=0000=0000\r\n"));
    CHECK(gs_run(&fixture, 0, "Z\r", "\r"));
    CHECK(fixture.dispatcher.display.center == GS232B_AZIMUTH_CENTER_SOUTH);
    for (size_t i = 0; i < GS_TEST_SESSIONS; ++i) {
        CHECK(gs232b_dispatcher_session_close(&fixture.dispatcher, fixture.sessions[i]) == GS232B_OK);
    }
    CHECK(gs_boot(&fixture));
    CHECK(fixture.dispatcher.display.mode == GS232B_AZIMUTH_DISPLAY_P45);
    CHECK(fixture.dispatcher.display.center == GS232B_AZIMUTH_CENTER_SOUTH);
    CHECK(fixture.dispatcher.display_revision == 3);
    CHECK(gs_run(&fixture, 0, "P36\r", "\r"));
    CHECK(gs_run(&fixture, 1, "C\r", "AZ=090\r\n"));
    return true;
}

static bool test_disabled_elevation_and_invalid_queries(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, false));
    CHECK(gs_run(&fixture, 0, "B\r", "EL=000\r\n"));
    CHECK(gs_run(&fixture, 1, "C2\r", "AZ=090  EL=000\r\n"));
    static const char *const rejected[] = {"U\r", "D\r", "O2\r", "F2\r", "W100 001\r"};
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        CHECK(gs_feed(&fixture, 0, rejected[i]) != GS232B_OK);
        CHECK(gs_expect(&fixture, 0, "?>\r\n"));
        CHECK(stopped(&fixture.sim));
    }
    CHECK(gs_run(&fixture, 0, "W100 000\r", "\r"));
    CHECK(gs_run(&fixture, 1, "E\r", "\r"));
    CHECK(gs_run(&fixture, 1, "S\r", "\r"));
    CHECK(gs_quiet(&fixture));
    return true;
}

int main(void)
{
    if (!test_queries_framing_and_actual_feedback() ||
        !test_manual_axes_stops_and_all_speeds() || !test_targets_and_slowdown() ||
        !test_firmware_speed_levels_and_slowdown() ||
        !test_modes_persistence_mapping_and_atomic_failure() ||
        !test_disabled_elevation_and_invalid_queries()) {
        return EXIT_FAILURE;
    }
    puts("GS commands, actual motion, query mapping and persistent modes passed");
    return EXIT_SUCCESS;
}
