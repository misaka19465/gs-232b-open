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

#include "calibration_test_support.h"

static bool test_confirmation_samples_now_and_only_origin(void)
{
    command_fixture_t fixture;
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        CHECK(command_fixture_init(&fixture, 0));
        CHECK(calibration_seed(&fixture, GS232B_AXIS_AZIMUTH));
        CHECK(calibration_seed(&fixture, GS232B_AXIS_ELEVATION));
        CHECK(command_run(&fixture, 0, axis == 0 ? "o\r" : "o2\r"));
        CHECK(calibration_response(&fixture, 0, axis == 0 ?
            "AZoffset\r\nare you sure? yes or no\r\n" :
            "ELoffset\r\nare you sure? yes or no\r\n"));
        CHECK(calibration_state(&fixture, axis == 0 ? GS232B_INTERACTION_ZERO_AZ :
            GS232B_INTERACTION_ZERO_EL, 0));
        CHECK(fixture.executed == 0);
        CHECK(calibration_stored(&fixture, (gs232b_axis_id_t)axis, 100000));
        CHECK(command_feed(&fixture, 1, "Y\r") == GS232B_ERROR_NOT_OWNER);
        CHECK(calibration_response(&fixture, 1, "?>\r\n"));
        CHECK(command_run(&fixture, 1, "\r"));
        CHECK(command_run(&fixture, 2, "N\r"));
        CHECK(fixture.control.calibration.kind != GS232B_INTERACTION_NONE);

        fixture.sim.now_us += STEP_US;
        fixture.sim.adc_override[axis] = true;
        fixture.sim.adc_uv[axis] = 200000;
        CHECK(command_run(&fixture, 0, axis == 0 ? "yEs\r" : "y\r"));
        CHECK(calibration_response(&fixture, 0, "Completed\r\n"));
        CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
        CHECK(calibration_stored(&fixture, (gs232b_axis_id_t)axis, 200000));
        CHECK(calibration_stored(&fixture, (gs232b_axis_id_t)(1 - axis), 100000));
        CHECK(stopped(&fixture.sim));
        CHECK(!fixture.store.records[GS232B_STORE_DISPLAY].present);
        CHECK(fixture.async_responses[1] == 0 && fixture.async_responses[2] == 0);
    }
    return true;
}

static bool test_answers_outside_and_queued_entry(void)
{
    command_fixture_t fixture;
    static const char *const answers[] = {"Y\r", "YES\r", "NO\r"};
    for (size_t i = 0; i < sizeof(answers) / sizeof(answers[0]); ++i) {
        CHECK(command_fixture_init(&fixture, 0));
        CHECK(command_feed(&fixture, 0, answers[i]) == GS232B_OK);
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(calibration_response(&fixture, 0, "?>\r\n"));
        CHECK(command_unowned(&fixture));
        CHECK(calibration_no_records(&fixture));
    }
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_feed(&fixture, 0, "O\rYeS\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.control.calibration.kind == GS232B_INTERACTION_ZERO_AZ);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(calibration_response(&fixture, 0, "Completed\r\n"));
    return true;
}

static bool test_cancel_and_invalid_input_cannot_move(void)
{
    command_fixture_t fixture;
    static const char *const cancel[] = {"N\r", "nO\r", "\r"};
    static const char *const invalid[] = {
        "R\r", "T\r", "C\r", "O2\r", "F\r", "X1\r", "P36\r", "Z\r", "bad\r"
    };
    for (size_t i = 0; i < sizeof(cancel) / sizeof(cancel[0]); ++i) {
        CHECK(command_fixture_init(&fixture, 0));
        CHECK(command_run(&fixture, 0, "O\r"));
        for (size_t j = 0; j < sizeof(invalid) / sizeof(invalid[0]); ++j) {
            CHECK(command_feed(&fixture, 0, invalid[j]) == GS232B_ERROR_INVALID_ARGUMENT);
            CHECK(calibration_response(&fixture, 0, "?>\r\n"));
            CHECK(fixture.control.calibration.kind == GS232B_INTERACTION_ZERO_AZ);
            CHECK(fixture.executed == 0);
        }
        CHECK(command_run(&fixture, 0, cancel[i]));
        CHECK(calibration_response(&fixture, 0, "\r"));
        CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
        CHECK(calibration_no_records(&fixture));
        CHECK(stopped(&fixture.sim));
    }
    return true;
}

static bool test_display_cadence_isolation_and_no_writes(void)
{
    command_fixture_t fixture;
    for (int both = 0; both < 2; ++both) {
        CHECK(command_fixture_init(&fixture, 0));
        CHECK(command_run(&fixture, 1, both ? "f2\r" : "f\r"));
        const char *expected = both ? "AZ=090  EL=090\r\n" : "AZ=090\r\n";
        CHECK(calibration_response(&fixture, 1, expected));
        CHECK(fixture.async_responses[1] == 0);
        CHECK(command_run(&fixture, 0, "\r"));
        CHECK(command_run(&fixture, 2, "N\r"));
        CHECK(command_feed(&fixture, 1, "YES\r") == GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(command_feed(&fixture, 1, "N\r") == GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(command_feed(&fixture, 1, "NO\r") == GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(command_feed(&fixture, 1, "M090\r") == GS232B_ERROR_BUSY);
        fixture.sim.now_us = GS232B_CALIBRATION_DISPLAY_PERIOD_US - 1;
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(fixture.async_responses[1] == 0);
        ++fixture.sim.now_us;
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(fixture.async_responses[1] == 1);
        CHECK(calibration_response(&fixture, 1, expected));
        fixture.sim.now_us += UINT64_C(2300000);
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(fixture.async_responses[1] == 2);
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(fixture.async_responses[1] == 2);
        fixture.sim.now_us += GS232B_CALIBRATION_DISPLAY_PERIOD_US - 1;
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(fixture.async_responses[1] == 2);
        ++fixture.sim.now_us;
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(fixture.async_responses[1] == 3);
        CHECK(fixture.async_responses[0] == 0 && fixture.async_responses[2] == 0);
        CHECK(command_run(&fixture, 1, "\r"));
        CHECK(calibration_response(&fixture, 1, "\r"));
        CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
        CHECK(calibration_no_records(&fixture));
        CHECK(stopped(&fixture.sim));
    }
    return true;
}

static bool test_all_stops_exit_all_interactions(void)
{
    static const char *const starts[] = {"O\r", "O2\r", "F\r", "F2\r"};
    static const char *const stops[] = {"A\r", "E\r", "S\r"};
    command_fixture_t fixture;
    for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        for (size_t j = 0; j < sizeof(stops) / sizeof(stops[0]); ++j) {
            for (size_t session = 0; session < CONTROL_TEST_SESSIONS; ++session) {
                CHECK(command_fixture_init(&fixture, 0));
                CHECK(command_run(&fixture, 0, starts[i]));
                const size_t replies = fixture.completes[0];
                if (session != 0) {
                    CHECK(command_feed(&fixture, 0, "Y") == GS232B_OK);
                }
                fixture.sim.now_us += GS232B_CALIBRATION_DISPLAY_PERIOD_US;
                fixture.respond_error = GS232B_ERROR_IO;
                CHECK(command_run(&fixture, session, stops[j]));
                CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
                CHECK(calibration_response(&fixture, session, "\r"));
                CHECK(fixture.completes[0] == replies + (session == 0 ? 1U : 0U));
                CHECK(fixture.async_responses[0] == 0);
                CHECK(j == 2 ? command_unowned(&fixture) : command_owner(&fixture, 0));
                if (session != 0) {
                    CHECK(command_feed(&fixture, 0, "ES\r") == GS232B_OK);
                }
                CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
                CHECK(calibration_no_records(&fixture));
                CHECK(stopped(&fixture.sim));
            }
        }
    }
    return true;
}

static bool test_display_uses_current_feedback_and_display_mapping(void)
{
    command_fixture_t fixture;
    static const char *const initial[] = {
        "AZ=450  EL=180\r\n", "AZ=270  EL=180\r\n", "AZ=090  EL=180\r\n"
    };
    static const char *const changed[] = {
        "AZ=090  EL=045\r\n", "AZ=270  EL=045\r\n", "AZ=090  EL=045\r\n"
    };
    for (size_t mode = 0; mode < 3; ++mode) {
        CHECK(command_fixture_init(&fixture, 0));
        if (mode != 0) {
            CHECK(command_run(&fixture, 0, "P36\r"));
        }
        if (mode == 2) {
            CHECK(command_run(&fixture, 0, "Z\r"));
        }
        fixture.sim.position_udeg[0] = INT64_C(450000000);
        fixture.sim.position_udeg[1] = INT64_C(180000000);
        fixture.sim.now_us += GS232B_FEEDBACK_SAMPLE_MAX_AGE_US + 1;
        CHECK(gs232b_motion_tick(&fixture.sim.motion) == GS232B_OK);
        CHECK(command_run(&fixture, 0, "F2\r"));
        CHECK(calibration_response(&fixture, 0, initial[mode]));
        fixture.sim.position_udeg[0] = INT64_C(90000000);
        fixture.sim.position_udeg[1] = INT64_C(45000000);
        fixture.sim.now_us += GS232B_CALIBRATION_DISPLAY_PERIOD_US;
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(calibration_response(&fixture, 0, changed[mode]));
        CHECK(calibration_no_records(&fixture));
    }
    return true;
}

static bool test_entry_preconditions_and_disabled_elevation(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "R\r"));
    CHECK(command_feed(&fixture, 0, "O\r") == GS232B_ERROR_BUSY);
    CHECK(command_run(&fixture, 0, "S\r"));
    CHECK(command_run(&fixture, 0, "M010 090 100\r"));
    CHECK(command_feed(&fixture, 0, "F\r") == GS232B_ERROR_BUSY);
    CHECK(command_run(&fixture, 0, "S\r"));
    gs232b_motion_axis_config_t config = fixture.sim.motion.config.axes[1];
    config.enabled = false;
    CHECK(gs232b_motion_configure_axis(&fixture.sim.motion, GS232B_AXIS_ELEVATION,
                                       &config) == GS232B_OK);
    CHECK(command_feed(&fixture, 0, "O2\r") == GS232B_ERROR_NOT_READY);
    CHECK(command_feed(&fixture, 0, "F2\r") == GS232B_ERROR_NOT_READY);
    CHECK(command_unowned(&fixture));
    CHECK(command_run(&fixture, 0, "F\r"));
    CHECK(command_run(&fixture, 1, "E\r"));
    config = fixture.sim.motion.config.axes[0];
    config.zero_valid = false;
    CHECK(gs232b_motion_configure_axis(&fixture.sim.motion, GS232B_AXIS_AZIMUTH,
                                       &config) == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(command_run(&fixture, 0, "O\r"));
    CHECK(command_run(&fixture, 0, "YES\r"));
    CHECK(fixture.sim.motion.config.axes[0].zero_valid);
    return true;
}

int main(void)
{
    if (!test_confirmation_samples_now_and_only_origin() ||
        !test_answers_outside_and_queued_entry() ||
        !test_cancel_and_invalid_input_cannot_move() ||
        !test_display_cadence_isolation_and_no_writes() ||
        !test_display_uses_current_feedback_and_display_mapping() ||
        !test_all_stops_exit_all_interactions() ||
        !test_entry_preconditions_and_disabled_elevation()) {
        return EXIT_FAILURE;
    }
    puts("session-isolated calibration confirmation, display and stop tests passed");
    return EXIT_SUCCESS;
}
