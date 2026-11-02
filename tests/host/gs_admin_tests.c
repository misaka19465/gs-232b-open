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

#include "management_test_support.h"

static bool authentication_rotation_and_reconnection(void)
{
    gs_fixture_t f; runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(gs_reopen_management(&f, 1, GS232B_TRANSPORT_ETHERNET_WS));
    CHECK(management_auth(&f, 1, "invalid", GS232B_ERROR_NOT_AUTHORIZED));
    char token[65], replacement[65];
    CHECK(management_enable(&f, token));
    CHECK(management_auth(&f, 1, "invalid", GS232B_ERROR_NOT_AUTHORIZED));
    CHECK(!f.dispatcher.sessions[1].management_authorized);
    CHECK(management_auth(&f, 1, token, GS232B_OK));
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(management_command(&f, 1, "!CFG BEGIN\r", GS232B_OK, NULL));
    CHECK(gs_run(&f, 0, "R\r", "\r"));
    CHECK(management_enable(&f, replacement));
    CHECK(strcmp(token, replacement) != 0);
    CHECK(!f.dispatcher.sessions[1].management_authorized && f.dispatcher.network_config.candidate_id == 0);
    CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, f.sessions[0]));
    CHECK(management_auth(&f, 1, token, GS232B_ERROR_NOT_AUTHORIZED));
    CHECK(management_auth(&f, 1, replacement, GS232B_OK));
    CHECK(management_command(&f, 1, "!CFG BEGIN\r", GS232B_OK, NULL));
    CHECK(management_command(&f, 0, "!ADMIN DISABLE\r", GS232B_OK, NULL));
    CHECK(!f.dispatcher.sessions[1].management_authorized && f.dispatcher.network_config.candidate_id == 0);
    CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, f.sessions[0]));
    CHECK(!f.store.records[GS232B_STORE_ADMIN].present && !f.store.records[GS232B_STORE_ADMIN].previous_present);
    CHECK(management_auth(&f, 1, replacement, GS232B_ERROR_NOT_AUTHORIZED));
    CHECK(gs_run(&f, 0, "S\r", "\r"));
    CHECK(management_enable(&f, token));
    CHECK(management_auth(&f, 1, token, GS232B_OK));
    CHECK(gs_reopen_management(&f, 1, GS232B_TRANSPORT_ETHERNET_WS));
    CHECK(management_command(&f, 1, "!CFG BEGIN\r", GS232B_ERROR_NOT_AUTHORIZED, NULL));
    CHECK(management_auth(&f, 1, token, GS232B_OK));
    gs232b_persisted_record_t record;
    const gs232b_store_t store = gs232b_fake_store_port(&f.store);
    CHECK(gs232b_persistence_load(&store, GS232B_STORE_ADMIN, &record) == GS232B_OK);
    CHECK(record.payload_size == 32);
    CHECK(record.schema_version == GS232B_PERSISTENCE_SCHEMA_ADMIN);
    return true;
}

static bool queued_authorization_is_rechecked(void)
{
    gs_fixture_t f; runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(gs_reopen_management(&f, 1, GS232B_TRANSPORT_ETHERNET_WS));
    char token[65];
    CHECK(management_enable(&f, token));
    CHECK(management_auth(&f, 1, token, GS232B_OK));
    CHECK(gs_feed(&f, 0, "!ADMIN DISABLE\r") == GS232B_OK);
    CHECK(gs_feed(&f, 1, "!CFG BEGIN\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_ERROR_NOT_AUTHORIZED);
    CHECK(gs_expect(&f, 0, "!{\"ok\":true,\"cmd\":\"ADMIN DISABLE\",\"data\":{\"enabled\":false}}\r\n"));
    CHECK(gs_expect(&f, 1, "!{\"ok\":false,\"cmd\":\"CFG BEGIN\",\"error\":\"NOT_AUTHORIZED\"}\r\n"));
    CHECK(f.dispatcher.network_config.candidate_id == 0);
    return true;
}

static bool authentication_survives_boot_and_storage_errors(void)
{
    gs_fixture_t f; runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    char token[65];
    CHECK(management_enable(&f, token));
    CHECK(gs232b_fake_fail_next(&f.store.failures[GS232B_STORE_ADMIN][GS232B_FAKE_STORE_SAVE],
        GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(management_command(&f, 0, "!ADMIN ENABLE\r", GS232B_ERROR_IO, NULL));
    CHECK(gs232b_fake_fail_next(&f.store.failures[GS232B_STORE_ADMIN][GS232B_FAKE_STORE_ERASE],
        GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(management_command(&f, 0, "!ADMIN DISABLE\r", GS232B_ERROR_IO, NULL));
    for (size_t i = 0; i < GS_TEST_SESSIONS; ++i) {
        CHECK(gs232b_dispatcher_session_close(&f.dispatcher, f.sessions[i]) == GS232B_OK);
    }
    CHECK(gs_boot(&f));
    CHECK(gs_reopen_management(&f, 1, GS232B_TRANSPORT_ETHERNET_WS));
    CHECK(!f.dispatcher.sessions[1].management_authorized);
    CHECK(management_auth(&f, 1, token, GS232B_OK));
    return true;
}

int main(void)
{
    return authentication_rotation_and_reconnection() && queued_authorization_is_rechecked() &&
        authentication_survives_boot_and_storage_errors() ? EXIT_SUCCESS : EXIT_FAILURE;
}
