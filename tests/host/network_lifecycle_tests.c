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

#include "network_test_support.h"

static bool test_concurrent_begin_and_stop(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(gs_feed(&f, 0, "!CFG BEGIN\r") == GS232B_OK);
    CHECK(gs_feed(&f, 2, "!CFG BEGIN\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(gs_expect(&f, 0, "!{\"ok\":true,\"cmd\":\"CFG BEGIN\",\"data\":{\"candidate_id\":1,\"base_revision\":7,\"idle_timeout_s\":60}}\r\n"));
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_ERROR_CONFLICT);
    CHECK(gs_expect(&f, 2, "!{\"ok\":false,\"cmd\":\"CFG BEGIN\",\"error\":\"CONFLICT\"}\r\n"));
    CHECK(rejected(&f, 2, "!CFG CHECK\r", GS232B_ERROR_CONFLICT));
    CHECK(rejected(&f, 2, "!NET SET {\"wifi\":{\"ssid\":\"other\"}}\r", GS232B_ERROR_CONFLICT));
    CHECK(rejected(&f, 2, "!CFG ABORT\r", GS232B_ERROR_CONFLICT));
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(gs_run(&f, 1, "R\r", "\r"));
    CHECK(gs_step(&f, 15));
    CHECK(f.sim.pwm_permille != 0);
    CHECK(response_contains(&f, 2, "!CFG STATUS\r", "\"candidate_id\":1"));
    CHECK(response_contains(&f, 1, "!NET GET\r", "\"candidate_id\":1"));
    CHECK(gs_run(&f, 1, "S\r", "\r"));
    CHECK(stopped(&f.sim));
    CHECK(gs232b_config_transaction_live(&f.dispatcher.network_config));
    CHECK(response_contains(&f, 0, "!CFG ABORT\r", "\"data\":{}"));
    CHECK(response_contains(&f, 2, "!CFG BEGIN\r", "\"candidate_id\":2"));
    CHECK(response_contains(&f, 2, "!NET SET {\"wifi\":{\"ssid\":\"softap-edit\"}}\r", "\"ok\":true"));
    CHECK(rejected(&f, 0, "!CFG CHECK\r", GS232B_ERROR_CONFLICT));
    CHECK(response_contains(&f, 2, "!CFG CHECK\r", "\"valid\":true"));
    CHECK(rejected(&f, 1, "!NET SET {}\r", GS232B_ERROR_NOT_AUTHORIZED));
    CHECK(gs_run(&f, 0, "S\r", "\r"));
    CHECK(gs232b_config_transaction_live(&f.dispatcher.network_config));
    return true;
}

static bool test_only_successful_configuration_requests_renew(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":1"));
    f.sim.now_us = GS232B_CONFIG_IDLE_TIMEOUT_US - 1U;
    CHECK(response_contains(&f, 0, "!NET GET\r", "\"idle_remaining_us\":1"));
    CHECK(response_contains(&f, 1, "!CFG STATUS\r", "\"idle_remaining_us\":1"));
    CHECK(rejected(&f, 0, "!NET SET {\"wifi\":{\"password\":\"bad\"}}\r", GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(rejected(&f, 2, "!CFG CHECK\r", GS232B_ERROR_CONFLICT));
    CHECK(f.dispatcher.network_config.renewed_us == 0);
    f.sim.now_us++;
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(f.dispatcher.network_config.candidate_id == 0);
    const gs232b_net_config_t empty = {0};
    CHECK(memcmp(&f.dispatcher.network_config.candidate, &empty, sizeof(empty)) == 0);
    CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":2"));
    f.sim.now_us += GS232B_CONFIG_IDLE_TIMEOUT_US - 1U;
    CHECK(response_contains(&f, 0, "!NET SET {\"wifi\":{\"ssid\":\"renewed\"}}\r", "\"ok\":true"));
    const gs232b_time_us_t set_at = f.sim.now_us;
    CHECK(f.dispatcher.network_config.renewed_us == set_at);
    f.sim.now_us += GS232B_CONFIG_IDLE_TIMEOUT_US - 1U;
    CHECK(response_contains(&f, 0, "!CFG CHECK\r", "\"valid\":true"));
    CHECK(f.dispatcher.network_config.renewed_us == f.sim.now_us);
    f.sim.now_us += GS232B_CONFIG_IDLE_TIMEOUT_US - 1U;
    CHECK(response_contains(&f, 0, "!CFG STATUS\r", "\"idle_remaining_us\":1"));
    f.sim.now_us++;
    CHECK(response_contains(&f, 1, "!NET GET\r", "\"candidate\":null"));
    CHECK(f.dispatcher.network_config.candidate_id == 0);
    CHECK(strcmp(f.dispatcher.network_config.active.wifi.ssid, "home") == 0);
    CHECK(!f.store.records[GS232B_STORE_NETWORK].present);
    return true;
}

static bool test_close_generation_tx_failure_and_queued_commands(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(gs_feed(&f, 0, "!CFG BEGIN\r!NET SET {\"wifi\":{\"ssid\":\"batched\"}}\r!CFG CHECK\r") == GS232B_OK);
    CHECK(gs_drain(&f));
    uint8_t bytes[4096]; size_t size;
    CHECK(gs232b_fake_sessions_take_tx(&f.io, f.sessions[0], bytes, sizeof(bytes), &size) == GS232B_OK);
    CHECK(strcmp(f.dispatcher.network_config.candidate.wifi.ssid, "batched") == 0);
    const gs232b_session_key_t old_session = f.sessions[0];
    CHECK(gs232b_dispatcher_session_close(&f.dispatcher, old_session) == GS232B_OK);
    CHECK(f.dispatcher.network_config.candidate_id == 0);
    CHECK(gs_open(&f, 0, GS232B_TRANSPORT_UART1));
    CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":2"));
    CHECK(gs232b_dispatcher_session_close(&f.dispatcher, old_session) == GS232B_ERROR_STALE_SESSION);
    CHECK(gs232b_config_transaction_live(&f.dispatcher.network_config));
    CHECK(gs_feed(&f, 0, "!NET SET {\"wifi\":{\"ssid\":\"cancelled\"}}\r") == GS232B_OK);
    CHECK(gs_feed(&f, 1, "S\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(gs_expect(&f, 0, "!{\"ok\":false,\"cmd\":\"NET SET\",\"error\":\"CONFLICT\"}\r\n"));
    CHECK(gs_expect(&f, 1, "\r"));
    CHECK(strcmp(f.dispatcher.network_config.candidate.wifi.ssid, "home") == 0);
    CHECK(gs232b_fake_sessions_fail_next(&f.io, f.sessions[0],
        GS232B_FAKE_SESSION_SEND, GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs_feed(&f, 0, "!NET SET {\"wifi\":{\"password\":\"new-secret\"}}\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_ERROR_IO);
    CHECK(f.dispatcher.network_config.candidate_id == 0);
    CHECK(strcmp(f.dispatcher.network_config.active.wifi.password, "prior-secret") == 0);
    return true;
}

static gs232b_time_us_t now_us(void *ctx) { return *(gs232b_time_us_t *)ctx; }

static bool test_clock_and_id_boundaries(void)
{
    gs232b_time_us_t now = 100;
    gs232b_config_transaction_t transaction;
    const gs232b_clock_t clock = {.ctx = &now, .now_us = now_us};
    const gs232b_session_key_t session = {1, 1};
    CHECK(gs232b_config_transaction_init(&transaction, clock, NULL, 9) == GS232B_OK);
    CHECK(gs232b_config_transaction_begin(&transaction, session) == GS232B_OK);
    now = 99;
    CHECK(gs232b_config_transaction_access(&transaction, session) == GS232B_ERROR_NOT_READY);
    gs232b_config_transaction_expire(&transaction);
    CHECK(transaction.candidate_id == 0);
    now = UINT64_MAX - 1U;
    CHECK(gs232b_config_transaction_begin(&transaction, session) == GS232B_OK);
    gs232b_net_changes_t changes;
    transaction.active_revision++;
    CHECK(gs232b_config_transaction_check(&transaction, session, &changes) == GS232B_ERROR_CONFLICT);
    transaction.active_revision--;
    CHECK(gs232b_config_transaction_check(&transaction, session, &changes) == GS232B_OK);
    CHECK(gs232b_config_transaction_abort(&transaction, session) == GS232B_OK);
    transaction.last_id = UINT32_MAX;
    CHECK(gs232b_config_transaction_begin(&transaction, session) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(transaction.candidate_id == 0);
    return true;
}

static bool test_small_response_has_no_transaction_side_effect(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    static const char *const commands[] = {"!CFG BEGIN", "!NET SET {\"wifi\":{\"ssid\":\"lost\"}}", "!CFG CHECK", "!CFG ABORT"};
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (i == 1) CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":1"));
        const gs232b_config_transaction_t before = f.dispatcher.network_config;
        gs232b_extension_request_t request;
        CHECK(gs232b_extension_parse((const uint8_t *)commands[i], strlen(commands[i]), &request) == GS232B_OK);
        request.context = gs_context(&f, 0);
        request.transport = GS232B_TRANSPORT_UART1;
        uint8_t tiny[1];
        gs232b_extension_response_t response;
        CHECK(gs232b_extension_dispatch(&f.dispatcher.extensions, &request, tiny, sizeof(tiny), &response) == GS232B_ERROR_BUFFER_TOO_SMALL);
        CHECK(memcmp(&f.dispatcher.network_config, &before, sizeof(before)) == 0);
    }
    return true;
}

bool network_lifecycle_tests(void)
{
    return test_concurrent_begin_and_stop() && test_only_successful_configuration_requests_renew() &&
        test_close_generation_tx_failure_and_queued_commands() && test_clock_and_id_boundaries() &&
        test_small_response_has_no_transaction_side_effect();
}
