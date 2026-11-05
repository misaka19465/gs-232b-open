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

static bool authentication_password_rotation_and_reconnection(void)
{
    gs_fixture_t f; runtime_t runtime = {0};
    const char *password = "device-admin-passphrase";
    const char *replacement = "rotated-admin-passphrase";
    CHECK(setup(&f, &runtime));
    CHECK(gs_reopen_management(&f, 1, GS232B_TRANSPORT_ETHERNET_WS));
    CHECK(management_auth(&f, 1, "invalid-password", GS232B_ERROR_NOT_AUTHORIZED));
    CHECK(management_set_password(&f, password));
    CHECK(management_command(&f, 1, "!SESSION AUTH {\"token\":\"obsolete-token\"}\r",
        GS232B_ERROR_INVALID_ARGUMENT, NULL));
    CHECK(management_auth(&f, 1, "invalid-password", GS232B_ERROR_NOT_AUTHORIZED));
    CHECK(!f.dispatcher.sessions[1].management_authorized);
    CHECK(management_auth(&f, 1, password, GS232B_OK));
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(management_command(&f, 1, "!CFG BEGIN\r", GS232B_OK, NULL));
    CHECK(gs_run(&f, 0, "R\r", "\r"));
    uint8_t previous_salt[GS232B_ADMIN_SALT_SIZE];
    memcpy(previous_salt, f.dispatcher.admin_salt, sizeof(previous_salt));
    CHECK(management_set_password(&f, replacement));
    CHECK(memcmp(previous_salt, f.dispatcher.admin_salt, sizeof(previous_salt)) != 0);
    CHECK(!f.dispatcher.sessions[1].management_authorized && f.dispatcher.network_config.candidate_id == 0);
    CHECK(management_auth(&f, 1, password, GS232B_ERROR_NOT_AUTHORIZED));
    CHECK(management_auth(&f, 1, replacement, GS232B_OK));
    CHECK(management_command(&f, 1, "!CFG BEGIN\r", GS232B_OK, NULL));
    CHECK(management_command(&f, 0, "!ADMIN DISABLE\r", GS232B_OK, NULL));
    CHECK(!f.dispatcher.sessions[1].management_authorized && f.dispatcher.network_config.candidate_id == 0);
    CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, f.sessions[0]));
    CHECK(!f.store.records[GS232B_STORE_ADMIN].present && !f.store.records[GS232B_STORE_ADMIN].previous_present);
    CHECK(management_auth(&f, 1, replacement, GS232B_ERROR_NOT_AUTHORIZED));
    CHECK(gs_run(&f, 0, "S\r", "\r"));
    CHECK(management_set_password(&f, password));
    CHECK(management_auth(&f, 1, password, GS232B_OK));
    CHECK(gs_reopen_management(&f, 1, GS232B_TRANSPORT_ETHERNET_WS));
    CHECK(management_command(&f, 1, "!CFG BEGIN\r", GS232B_ERROR_NOT_AUTHORIZED, NULL));
    CHECK(management_auth(&f, 1, password, GS232B_OK));
    gs232b_persisted_record_t record;
    const gs232b_store_t store = gs232b_fake_store_port(&f.store);
    CHECK(gs232b_persistence_load(&store, GS232B_STORE_ADMIN, &record) == GS232B_OK);
    CHECK(record.payload_size == GS232B_ADMIN_CREDENTIAL_SIZE);
    CHECK(record.schema_version == GS232B_PERSISTENCE_SCHEMA_ADMIN);
    for (size_t i = 0; i + strlen(password) <= record.payload_size; ++i) {
        CHECK(memcmp(record.payload + i, password, strlen(password)) != 0);
    }
    return true;
}

static bool queued_authorization_is_rechecked(void)
{
    gs_fixture_t f; runtime_t runtime = {0};
    const char *password = "queued-admin-password";
    CHECK(setup(&f, &runtime));
    CHECK(gs_reopen_management(&f, 1, GS232B_TRANSPORT_ETHERNET_WS));
    CHECK(management_set_password(&f, password));
    CHECK(management_auth(&f, 1, password, GS232B_OK));
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
    const char *password = "storage-admin-password";
    const char *replacement = "replacement-admin-password";
    CHECK(setup(&f, &runtime));
    CHECK(management_set_password(&f, password));
    CHECK(gs232b_fake_fail_next(&f.store.failures[GS232B_STORE_ADMIN][GS232B_FAKE_STORE_SAVE],
        GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(management_command(&f, 0,
        "!ADMIN SET {\"password\":\"replacement-admin-password\"}\r", GS232B_ERROR_IO, NULL));
    CHECK(management_command(&f, 0, "!ADMIN SET {\"password\":\"short\"}\r",
        GS232B_ERROR_INVALID_ARGUMENT, NULL));
    CHECK(management_auth(&f, 1, password, GS232B_OK));
    CHECK(management_auth(&f, 1, replacement, GS232B_ERROR_NOT_AUTHORIZED));
    CHECK(gs232b_fake_fail_next(&f.store.failures[GS232B_STORE_ADMIN][GS232B_FAKE_STORE_ERASE],
        GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(management_command(&f, 0, "!ADMIN DISABLE\r", GS232B_ERROR_IO, NULL));
    for (size_t i = 0; i < GS_TEST_SESSIONS; ++i) {
        CHECK(gs232b_dispatcher_session_close(&f.dispatcher, f.sessions[i]) == GS232B_OK);
    }
    CHECK(gs_boot(&f));
    CHECK(gs_reopen_management(&f, 1, GS232B_TRANSPORT_ETHERNET_WS));
    CHECK(!f.dispatcher.sessions[1].management_authorized);
    CHECK(management_auth(&f, 1, password, GS232B_OK));
    return true;
}

static bool old_admin_record_requires_flash_erase(void)
{
    gs_fixture_t f; runtime_t runtime = {0};
    static const uint8_t old_token_digest[32] = {0};
    CHECK(setup(&f, &runtime));
    const gs232b_store_t store = gs232b_fake_store_port(&f.store);
    uint64_t revision = 0;
    CHECK(gs232b_persistence_commit(&store, GS232B_STORE_ADMIN, 0,
        1U, old_token_digest, sizeof(old_token_digest), &revision) == GS232B_OK);
    CHECK(revision == 1U);
    for (size_t i = 0; i < GS_TEST_SESSIONS; ++i) {
        CHECK(gs232b_dispatcher_session_close(&f.dispatcher, f.sessions[i]) == GS232B_OK);
    }
    CHECK(gs232b_fake_sessions_init(&f.io, f.sim.manager.clock) == GS232B_OK);
    CHECK(gs232b_dispatcher_init(&f.dispatcher, &f.sim.motion, store,
        gs232b_fake_sessions_port(&f.io), f.request_bytes, sizeof(f.request_bytes)) ==
        GS232B_ERROR_CORRUPT);
    return true;
}

int main(void)
{
    return authentication_password_rotation_and_reconnection() && queued_authorization_is_rechecked() &&
        authentication_survives_boot_and_storage_errors() && old_admin_record_requires_flash_erase() ?
        EXIT_SUCCESS : EXIT_FAILURE;
}
