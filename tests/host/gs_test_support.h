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

#ifndef GS232B_GS_TEST_SUPPORT_H
#define GS232B_GS_TEST_SUPPORT_H

#include "gs232b/gs_dispatcher.h"
#include "gs232b/fakes.h"
#include "motion_test_support.h"

#include <string.h>

#define GS_TEST_SESSIONS 3U
#define GS_SECOND_US UINT64_C(1000000)

typedef struct {
    gs232b_motion_sim_t sim;
    gs232b_fake_store_t store;
    gs232b_fake_sessions_t io;
    gs232b_dispatcher_t dispatcher;
    uint8_t request_bytes[8192];
    uint8_t lines[GS_TEST_SESSIONS][GS232B_LINE_STORAGE_BYTES];
    gs232b_session_key_t sessions[GS_TEST_SESSIONS];
} gs_fixture_t;

static inline bool gs_expect(gs_fixture_t *fixture, size_t session, const char *expected)
{
    uint8_t bytes[4096];
    size_t size;
    const gs232b_error_t error = gs232b_fake_sessions_take_tx(&fixture->io,
        fixture->sessions[session], bytes, sizeof(bytes), &size);
    CHECK(error == GS232B_OK || (expected[0] == '\0' && error == GS232B_ERROR_WOULD_BLOCK));
    if (size != strlen(expected) || memcmp(bytes, expected, size) != 0) {
        fprintf(stderr, "session %zu: got [%.*s] (%zu), expected [%s] (%zu)\n",
            session, (int)size, bytes, size, expected, strlen(expected));
        CHECK(false);
    }
    return true;
}

static inline bool gs_open(gs_fixture_t *fixture, size_t index, gs232b_transport_t transport)
{
    gs232b_session_key_t io_key;
    CHECK(gs232b_fake_sessions_open(&fixture->io, transport, &io_key) == GS232B_OK);
    CHECK(gs232b_dispatcher_session_open(&fixture->dispatcher, transport,
        fixture->lines[index], sizeof(fixture->lines[index]), &fixture->sessions[index]) == GS232B_OK);
    CHECK(gs232b_session_key_equal(io_key, fixture->sessions[index]));
    return gs_expect(fixture, index, "Connect OK\r\n");
}

static inline bool gs_boot(gs_fixture_t *fixture)
{
    CHECK(gs232b_fake_sessions_init(&fixture->io, fixture->sim.manager.clock) == GS232B_OK);
    CHECK(gs232b_dispatcher_init(&fixture->dispatcher, &fixture->sim.motion,
        gs232b_fake_store_port(&fixture->store), gs232b_fake_sessions_port(&fixture->io),
        fixture->request_bytes, sizeof(fixture->request_bytes)) == GS232B_OK);
    for (size_t i = 0; i < GS_TEST_SESSIONS; ++i) {
        CHECK(gs_open(fixture, i, (gs232b_transport_t)i));
    }
    return true;
}

static inline bool gs_init(gs_fixture_t *fixture, bool elevation)
{
    memset(fixture, 0, sizeof(*fixture));
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    config.model_id = elevation ? 1U : 5U;
    config.axes[GS232B_AXIS_ELEVATION].enabled = elevation;
    CHECK(gs232b_motion_sim_init(&fixture->sim, &config, 0, WAIT_US) == GS232B_OK);
    gs232b_fake_store_init(&fixture->store);
    return gs_boot(fixture);
}

static inline gs232b_command_context_t gs_context(gs_fixture_t *fixture, size_t session)
{
    gs232b_command_context_t context = {0};
    gs232b_dispatcher_capture(&fixture->dispatcher, fixture->sessions[session], &context);
    return context;
}

static inline gs232b_error_t gs_feed(gs_fixture_t *fixture, size_t session, const char *text)
{
    return gs232b_dispatcher_feed(&fixture->dispatcher, gs_context(fixture, session),
        (const uint8_t *)text, strlen(text));
}

static inline bool gs_run(gs_fixture_t *fixture, size_t session,
                            const char *line, const char *response)
{
    CHECK(gs_feed(fixture, session, line) == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&fixture->dispatcher) == GS232B_OK);
    return gs_expect(fixture, session, response);
}

static inline bool gs_drain(gs_fixture_t *fixture)
{
    for (size_t i = 0; i < 64; ++i) {
        if (fixture->dispatcher.control.request_count == 0 &&
            fixture->dispatcher.control.stop_mask == 0) {
            return true;
        }
        CHECK(gs232b_dispatcher_tick(&fixture->dispatcher) == GS232B_OK);
    }
    CHECK(false);
}

static inline bool gs_step(gs_fixture_t *fixture, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        CHECK(gs232b_motion_sim_step(&fixture->sim, STEP_US) == GS232B_OK);
        CHECK(gs232b_dispatcher_tick(&fixture->dispatcher) == GS232B_OK);
        CHECK(!fixture->sim.unsafe_output);
    }
    return true;
}

static inline bool gs_quiet(gs_fixture_t *fixture)
{
    for (size_t i = 0; i < GS_TEST_SESSIONS; ++i) {
        CHECK(gs_expect(fixture, i, ""));
    }
    return true;
}

#endif
