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

static bool test_timing_queries_and_cross_type_replacement(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_run(&fixture, 0, "W001 090 090 100 100 110 110\r", "\r"));
    CHECK(gs_run(&fixture, 1, "N\r", "=0001=0003\r\n"));
    CHECK(gs_run(&fixture, 0, "T\r", "\r"));
    fixture.sim.now_us = GS_SECOND_US - 1;
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
    CHECK(gs_run(&fixture, 0, "T\r", "\r"));
    CHECK(gs_run(&fixture, 1, "N\r", "=0001=0003\r\n"));
    CHECK(gs_run(&fixture, 0, "X2\r", "\r"));
    fixture.sim.now_us = GS_SECOND_US;
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
    CHECK(gs_run(&fixture, 2, "N\r", "=0002=0003\r\n"));
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&fixture.sim, &state));
    CHECK(state.axes[0].target_mdeg == 100000 && state.axes[1].target_mdeg == 100000);
    CHECK(gs_run(&fixture, 0, "M120\r", "\r"));
    CHECK(snapshot(&fixture.sim, &state));
    CHECK(state.axes[0].target_mdeg == 120000 && !state.axes[1].target_valid);
    CHECK(state.output.last_successful.elevation == GS232B_EL_STOP);
    CHECK(gs_run(&fixture, 1, "N\r", "=0000=0000\r\n"));
    CHECK(gs_run(&fixture, 0, "M001 120 130\r", "\r"));
    CHECK(gs_run(&fixture, 0, "W001 090 090 100 100\r", "\r"));
    CHECK(gs_run(&fixture, 0, "T\r", "\r"));
    fixture.sim.now_us += GS_SECOND_US;
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
    CHECK(gs_run(&fixture, 2, "N\r", "=0002=0002\r\n"));
    CHECK(gs_feed(&fixture, 0, "T\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs_expect(&fixture, 0, "?>\r\n"));
    CHECK(gs_run(&fixture, 2, "S\r", "\r"));
    CHECK(gs_run(&fixture, 1, "N\r", "=0000=0000\r\n"));
    CHECK(stopped(&fixture.sim));
    CHECK(gs_quiet(&fixture));
    return true;
}

static bool test_bare_mw_authorization_precedes_side_effects(void)
{
    static const char *const sequences[] = {"M010 180 200\r", "W010 180 120 200 130\r"};
    static const char *const bare[] = {" m   \r", "w\r"};
    for (size_t i = 0; i < 2; ++i) {
        for (size_t j = 0; j < 2; ++j) {
            gs_fixture_t fixture;
            CHECK(gs_init(&fixture, true));
            CHECK(gs_run(&fixture, 0, sequences[i], "\r"));
            CHECK(gs_run(&fixture, 0, "T\r", "\r"));
            CHECK(gs_step(&fixture, 15));
            const gs232b_sequence_snapshot_t sequence = fixture.dispatcher.control.sequence_state;
            gs232b_motion_snapshot_t before, after;
            CHECK(snapshot(&fixture.sim, &before));
            const size_t attempts = fixture.sim.output_attempts;
            CHECK(gs_feed(&fixture, 1, bare[j]) == GS232B_ERROR_NOT_OWNER);
            CHECK(gs_expect(&fixture, 1, "?>\r\n"));
            CHECK(fixture.sim.output_attempts == attempts);
            CHECK(snapshot(&fixture.sim, &after));
            CHECK(memcmp(before.axes, after.axes, sizeof(before.axes)) == 0);
            CHECK(memcmp(&sequence, &fixture.dispatcher.control.sequence_state,
                         sizeof(sequence)) == 0);
            CHECK(gs_run(&fixture, 2, "N\r", "=0001=0002\r\n"));
            CHECK(gs_feed(&fixture, 0, bare[j]) == GS232B_OK);
            CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_ERROR_INVALID_ARGUMENT);
            CHECK(gs_expect(&fixture, 0, "?>\r\n"));
            CHECK(stopped(&fixture.sim));
            CHECK(gs_run(&fixture, 2, "N\r", "=0000=0000\r\n"));
            CHECK(gs_step(&fixture, 550));
            CHECK(stopped(&fixture.sim));
            CHECK(gs_quiet(&fixture));
        }
    }
    return true;
}

static bool test_malformed_uploads_preserve_active_sequence(void)
{
    static const char *const invalid[] = {
        "M000 100 120\r", "W001 100 090 120 181\r", "M001 100 999\r",
        "M 100\r", "M10\r", "W100\r", "W001 100 090 120\r"
    };
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_run(&fixture, 0, "M010 100 120\r", "\r"));
    CHECK(gs_run(&fixture, 0, "T\r", "\r"));
    const gs232b_time_us_t deadline = fixture.dispatcher.control.sequence_state.next_deadline_us;
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(gs_feed(&fixture, 0, invalid[i]) != GS232B_OK);
        CHECK(gs_expect(&fixture, 0, "?>\r\n"));
        CHECK(gs_run(&fixture, 1, "N\r", "=0001=0002\r\n"));
        CHECK(fixture.dispatcher.control.sequence_state.next_deadline_us == deadline);
    }
    CHECK(gs_feed(&fixture, 0, "M001 100 120") == GS232B_OK);
    fixture.sim.now_us = 2 * GS_SECOND_US;
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
    CHECK(gs_expect(&fixture, 0, "?>\r\n"));
    CHECK(gs_feed(&fixture, 0, " 130\r") == GS232B_OK);
    CHECK(gs_expect(&fixture, 0, ""));
    CHECK(gs_run(&fixture, 1, "N\r", "=0001=0002\r\n"));
    CHECK(gs_run(&fixture, 2, "S\r", "\r"));
    return true;
}

static bool test_maximum_uploads_reach_production_executor(void)
{
    for (size_t paired = 0; paired < 2; ++paired) {
        gs_fixture_t fixture;
        CHECK(gs_init(&fixture, true));
        CHECK(gs_feed(&fixture, 0, paired ? "W999" : "M999") == GS232B_OK);
        const size_t points = paired ? 1900 : 3800;
        for (size_t i = 0; i < points; ++i) {
            CHECK(gs_feed(&fixture, 0, paired ? " 090 090" : " 090") == GS232B_OK);
            if (i % 100 == 0) {
                CHECK(gs_run(&fixture, 1, "C\r", "AZ=090\r\n"));
                CHECK(fixture.dispatcher.control.sequence_state.state == GS232B_SEQUENCE_EMPTY);
                CHECK(stopped(&fixture.sim));
            }
        }
        CHECK(gs_run(&fixture, 0, "\r", "\r"));
        CHECK(gs_run(&fixture, 2, "N\r", paired ? "=0001=1900\r\n" : "=0001=3800\r\n"));
        CHECK(gs_run(&fixture, 0, "T\r", "\r"));
        CHECK(gs_run(&fixture, 1, "S\r", "\r"));
        CHECK(stopped(&fixture.sim));
        CHECK(gs_quiet(&fixture));
    }
    return true;
}

int main(void)
{
    if (!test_timing_queries_and_cross_type_replacement() ||
        !test_bare_mw_authorization_precedes_side_effects() ||
        !test_malformed_uploads_preserve_active_sequence() ||
        !test_maximum_uploads_reach_production_executor()) {
        return EXIT_FAILURE;
    }
    puts("GS sequence wire responses, maximum uploads and authorized bare M/W passed");
    return EXIT_SUCCESS;
}
