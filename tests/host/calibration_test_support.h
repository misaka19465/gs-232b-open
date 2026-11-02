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

#ifndef GS232B_CALIBRATION_TEST_SUPPORT_H
#define GS232B_CALIBRATION_TEST_SUPPORT_H

#include "command_test_support.h"

static inline bool calibration_response(command_fixture_t *fixture, size_t session,
                                          const char *expected)
{
    CHECK(fixture->response_size[session] == strlen(expected));
    if (memcmp(fixture->response[session], expected, strlen(expected)) != 0) {
        fprintf(stderr, "response mismatch: got [%.*s], expected [%s]\n",
            (int)fixture->response_size[session], fixture->response[session], expected);
    }
    CHECK(memcmp(fixture->response[session], expected, strlen(expected)) == 0);
    return true;
}

static inline bool calibration_state(command_fixture_t *fixture,
                                       gs232b_interaction_t kind, size_t session)
{
    gs232b_control_snapshot_t state;
    CHECK(gs232b_control_snapshot(&fixture->control, &state) == GS232B_OK);
    CHECK(state.activity.interaction == kind);
    if (kind != GS232B_INTERACTION_NONE) {
        CHECK(gs232b_session_key_equal(fixture->control.calibration.session,
                                       fixture->sessions[session]));
        CHECK(command_owner(fixture, session));
    }
    return true;
}

static inline bool calibration_seed(command_fixture_t *fixture, gs232b_axis_id_t axis)
{
    const gs232b_feedback_reading_t reading = {
        .acquired_at_us = fixture->sim.now_us,
        .adc_uv = fixture->sim.motion.config.axes[axis].zero_uv,
        .valid = true
    };
    return gs232b_feedback_zero_save(&fixture->control.executor.calibration.store,
        axis, &reading, fixture->sim.now_us) == GS232B_OK;
}

static inline bool calibration_stored(command_fixture_t *fixture,
                                       gs232b_axis_id_t axis, uint32_t expected)
{
    uint32_t zero_uv;
    CHECK(gs232b_feedback_zero_load(&fixture->control.executor.calibration.store,
                                    axis, &zero_uv) == GS232B_OK);
    CHECK(zero_uv == expected);
    CHECK(fixture->sim.motion.config.axes[axis].zero_valid);
    CHECK(fixture->sim.motion.config.axes[axis].zero_uv == expected);
    return true;
}

static inline bool calibration_no_records(command_fixture_t *fixture)
{
    for (int i = 0; i < GS232B_STORE_DOMAIN_COUNT; ++i) {
        CHECK(!fixture->store.records[i].present);
    }
    return true;
}

#endif
