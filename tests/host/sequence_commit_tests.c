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

#include "command_test_support.h"

static bool configure_axis(command_fixture_t *fixture, gs232b_axis_id_t axis,
                             bool enabled, int32_t minimum, int32_t maximum)
{
    gs232b_motion_axis_config_t config = fixture->sim.motion.config.axes[axis];
    config.enabled = enabled;
    config.minimum_mdeg = minimum;
    config.maximum_mdeg = maximum;
    CHECK(gs232b_motion_configure_axis(&fixture->sim.motion, axis, &config) == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture->control) == GS232B_OK);
    return true;
}

static bool test_full_validation_and_execution_recheck(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "W005 090 090 100 095\r"));
    const gs232b_sequence_data_t old = fixture.control.sequence.active;
    const size_t executed = fixture.executed;
    CHECK(command_feed(&fixture, 0, "M010 120 240\r") == GS232B_OK);
    fixture.control.calibration.kind = GS232B_INTERACTION_ZERO_AZ;
    fixture.control.calibration.session = fixture.sessions[0];
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_BUSY);
    CHECK(fixture.control.sequence.active.angles == old.angles);
    CHECK(fixture.control.sequence.active.command == 'W');
    CHECK(fixture.executed == executed);
    CHECK(!gs232b_session_key_valid(fixture.control.input_reservation.session));
    fixture.control.calibration.kind = GS232B_INTERACTION_NONE;

    CHECK(command_feed(&fixture, 0, "M010 120 240\r") == GS232B_OK);
    fixture.execute_error = GS232B_ERROR_NOT_READY;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_NOT_READY);
    CHECK(fixture.control.sequence.active.angles == old.angles);
    CHECK(fixture.control.sequence.active.point_count == old.point_count);
    fixture.execute_error = GS232B_OK;

    CHECK(command_feed(&fixture, 0, "M010 120 240\r") == GS232B_OK);
    gs232b_motion_axis_config_t config = fixture.sim.motion.config.axes[0];
    config.maximum_mdeg = 200000;
    CHECK(gs232b_motion_configure_axis(&fixture.sim.motion, GS232B_AXIS_AZIMUTH,
                                       &config) == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(fixture.control.sequence.active.angles == old.angles);
    CHECK(fixture.control.sequence.active.command == 'W');
    CHECK(fixture.executed == executed + 1);
    CHECK(!gs232b_session_key_valid(fixture.control.input_reservation.session));
    return true;
}

static bool test_axis_capabilities_limits_and_p36_continuity(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "M005 090 100\r"));
    const uint16_t *old = fixture.control.sequence.active.angles;
    CHECK(configure_axis(&fixture, GS232B_AXIS_ELEVATION, false, 0, 180000));
    CHECK(command_feed(&fixture, 0, "W010 120 000 240 001\r") == GS232B_ERROR_NOT_READY);
    CHECK(fixture.control.sequence.active.angles == old);
    CHECK(command_run(&fixture, 0, "W010 090 000 180 000\r"));
    CHECK(fixture.control.sequence.first.axis_mask == GS232B_MOTION_AZ_MASK);
    CHECK(!fixture.sim.motion.state.axes[1].target_valid);

    CHECK(configure_axis(&fixture, GS232B_AXIS_ELEVATION, true, 0, 120000));
    old = fixture.control.sequence.active.angles;
    CHECK(command_feed(&fixture, 0, "W010 090 090 180 121\r") == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(fixture.control.sequence.active.angles == old);

    CHECK(command_fixture_init(&fixture, 0));
    fixture.display.mode = GS232B_AZIMUTH_DISPLAY_P36;
    fixture.display.center = GS232B_AZIMUTH_CENTER_SOUTH;
    fixture.sim.position_udeg[0] = INT64_C(400000000);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(configure_axis(&fixture, GS232B_AXIS_AZIMUTH, true, 100000, 450000));
    CHECK(command_run(&fixture, 0, "M010 060 100 060 100\r"));
    const uint16_t *angles = fixture.control.sequence.active.angles;
    CHECK(angles[0] == 420 && angles[1] == 100 && angles[2] == 420 && angles[3] == 100);
    CHECK(fixture.sim.motion.state.axes[0].target_mdeg == 420000);
    CHECK(command_feed(&fixture, 0, "M010 060 361\r") == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(fixture.control.sequence.active.angles == angles);
    return true;
}

static bool test_idle_owner_kept_during_upload_and_error_releases_reservation(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "X1\r"));
    CHECK(command_feed(&fixture, 0, "M010 ") == GS232B_OK);
    CHECK(gs232b_control_release(&fixture.control, command_context(&fixture, 0)) ==
          GS232B_ERROR_BUSY);
    for (size_t i = 0; i < 4; ++i) {
        fixture.sim.now_us += GS232B_CONTROL_IDLE_RELEASE_US - 1;
        CHECK(command_feed(&fixture, 0, "090 ") == GS232B_OK);
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(command_owner(&fixture, 0));
    }
    CHECK(command_run(&fixture, 0, "\r"));
    CHECK(fixture.control.sequence.active.point_count == 4);
    CHECK(command_run(&fixture, 1, "S\r"));
    CHECK(command_feed(&fixture, 0, "M010 100 x") == GS232B_OK);
    CHECK(!gs232b_session_key_valid(fixture.control.input_reservation.session));
    CHECK(command_run(&fixture, 1, "M010 090 180\r"));
    const uint16_t *angles = fixture.control.sequence.active.angles;
    CHECK(command_feed(&fixture, 0, "\r") == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(fixture.control.sequence.active.angles == angles);
    CHECK(command_owner(&fixture, 1));
    return true;
}

static bool test_bare_single_and_cross_type_replacement(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "W010 120 135 240 090\r"));
    CHECK(fixture.sim.motion.state.axes[1].target_valid);
    CHECK(command_run(&fixture, 0, "M010 180 270\r"));
    CHECK(fixture.control.sequence.active.command == 'M');
    CHECK(!fixture.sim.motion.state.axes[1].target_valid);
    CHECK(fixture.sim.motion.state.axes[0].target_mdeg == 180000);
    CHECK(command_run(&fixture, 0, "W150 060\r"));
    CHECK(fixture.control.sequence.active.point_count == 0);
    CHECK(fixture.control.sequence_state.state == GS232B_SEQUENCE_EMPTY);
    CHECK(fixture.sim.motion.state.axes[0].target_mdeg == 150000);
    CHECK(fixture.sim.motion.state.axes[1].target_mdeg == 60000);
    CHECK(command_run(&fixture, 0, "W010 180 090 270 010\r"));
    CHECK(command_feed(&fixture, 0, " M   \r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(fixture.control.sequence.active.point_count == 0);
    CHECK(fixture.control.sequence_state.state == GS232B_SEQUENCE_EMPTY);
    CHECK(stopped(&fixture.sim));

    CHECK(command_run(&fixture, 0, "U\r"));
    CHECK(command_run(&fixture, 0, "M180\r"));
    CHECK(fixture.sim.motion.axes[1].intent == GS232B_AXIS_MANUAL);
    CHECK(command_run(&fixture, 1, "S\r"));
    return true;
}

static bool test_upload_close_queue_full_and_fault(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_feed(&fixture, 0, "M010 100 ") == GS232B_OK);
    CHECK(gs232b_control_session_close(&fixture.control, fixture.sessions[0]) == GS232B_OK);
    CHECK(!gs232b_session_key_valid(fixture.control.input_reservation.session));
    CHECK(command_feed(&fixture, 1, "W010 090 090 100 100\r") == GS232B_OK);
    CHECK(gs232b_control_session_close(&fixture.control, fixture.sessions[1]) == GS232B_OK);
    CHECK(fixture.control.request_count == 0 && !fixture.control.upload_queued);
    CHECK(command_unowned(&fixture));
    CHECK(command_run(&fixture, 2, "M010 090 100\r"));
    const uint16_t *old = fixture.control.sequence.active.angles;
    for (size_t i = 0; i < GS232B_CONTROL_REQUESTS_PER_SESSION; ++i) {
        CHECK(command_feed(&fixture, 2, "C\r") == GS232B_OK);
    }
    CHECK(command_feed(&fixture, 2, "M010 120 240\r") == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(!gs232b_session_key_valid(fixture.control.input_reservation.session));
    CHECK(fixture.control.sequence.active.angles == old);
    for (size_t i = 0; i < GS232B_CONTROL_REQUESTS_PER_SESSION; ++i) {
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    }
    CHECK(command_feed(&fixture, 2, "W010 120 135 240 090\r") == GS232B_OK);
    fixture.sim.now_us += WAIT_US;
    fixture.sim.output_failures = 1;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_IO);
    CHECK(fixture.control.sequence.active.point_count == 0);
    CHECK(!gs232b_session_key_valid(fixture.control.input_reservation.session));
    CHECK(command_unowned(&fixture));
    CHECK(stopped(&fixture.sim));
    return true;
}

int main(void)
{
    if (!test_full_validation_and_execution_recheck() ||
        !test_axis_capabilities_limits_and_p36_continuity() ||
        !test_bare_single_and_cross_type_replacement() ||
        !test_idle_owner_kept_during_upload_and_error_releases_reservation() ||
        !test_upload_close_queue_full_and_fault()) {
        return EXIT_FAILURE;
    }
    puts("sequence commit state, axes, lifecycle and failure tests passed");
    return EXIT_SUCCESS;
}
