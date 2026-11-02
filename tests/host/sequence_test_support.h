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

#ifndef GS232B_SEQUENCE_TEST_SUPPORT_H
#define GS232B_SEQUENCE_TEST_SUPPORT_H

#include "command_test_support.h"

#define SECOND_US UINT64_C(1000000)

static inline bool sequence_state(command_fixture_t *fixture,
    gs232b_sequence_state_t expected, uint16_t point, uint16_t count,
    gs232b_time_us_t deadline_us)
{
    gs232b_control_snapshot_t state;
    CHECK(gs232b_control_snapshot(&fixture->control, &state) == GS232B_OK);
    CHECK(state.sequence.state == expected);
    CHECK(state.sequence.current_point == point);
    CHECK(state.sequence.point_count == count);
    CHECK(state.sequence.next_deadline_us == deadline_us);
    CHECK(!fixture->sim.unsafe_output);
    return true;
}

static inline bool sequence_response(command_fixture_t *fixture, size_t session,
                                       const char *expected)
{
    CHECK(fixture->response_size[session] == strlen(expected));
    CHECK(memcmp(fixture->response[session], expected, strlen(expected)) == 0);
    return true;
}

static inline bool sequence_query(command_fixture_t *fixture, size_t session,
                                    const char *expected)
{
    CHECK(command_run(fixture, session, "N\r"));
    return sequence_response(fixture, session, expected);
}

static inline bool sequence_at(command_fixture_t *fixture, gs232b_time_us_t now_us)
{
    fixture->sim.now_us = now_us;
    CHECK(gs232b_control_tick(&fixture->control) == GS232B_OK);
    CHECK(!fixture->sim.unsafe_output);
    return true;
}

static inline bool sequence_fault(command_fixture_t *fixture,
    gs232b_fault_code_t code, gs232b_error_t error)
{
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&fixture->sim, &state));
    CHECK(state.fault.code == code && state.fault.error == error);
    CHECK(!state.axes[0].target_valid && !state.axes[1].target_valid);
    CHECK(sequence_state(fixture, GS232B_SEQUENCE_EMPTY, 0, 0, 0));
    CHECK(fixture->control.sequence.active.point_count == 0);
    CHECK(command_unowned(fixture));
    return true;
}

#endif
