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

typedef struct {
    const char *command;
    const char *prepare;
    size_t success_session;
    size_t denied_session;
    bool expired_ap;
} command_case_t;

static const command_case_t cases[] = {
    {"!SYS INFO\r", NULL, 0, 2, true},
    {"!NET GET\r", NULL, 0, 2, true},
    {"!ADMIN ENABLE\r", NULL, 0, 1, false},
    {"!ADMIN DISABLE\r", NULL, 0, 1, false},
    {"!SESSION AUTH {\"token\":\"%s\"}\r", "auth", 1, 0, false},
    {"!CFG BEGIN\r", NULL, 0, 1, false},
    {"!NET SET {\"wifi\":{\"ssid\":\"new-home\"}}\r", "candidate", 0, 1, false},
    {"!CFG CHECK\r", "candidate", 0, 1, false},
    {"!CFG APPLY {\"candidate_id\":1,\"timeout_s\":30}\r", "candidate", 0, 1, false},
    {"!CFG STATUS\r", NULL, 0, 2, true},
    {"!CFG CONFIRM {\"trial_id\":1}\r", "trial", 0, 1, false},
    {"!CFG ABORT\r", "candidate", 0, 1, false},
    {"!PROV START {\"timeout_s\":600}\r", NULL, 0, 1, false},
    {"!PROV STATUS\r", NULL, 0, 2, true},
    {"!PROV STOP\r", NULL, 0, 1, false},
    {"!NET RESET {\"confirm\":\"NETWORK_ONLY\"}\r", NULL, 0, 1, false},
    {"!OWNER GET\r", NULL, 0, 2, false},
    {"!OWNER TAKE\r", NULL, 0, 2, false},
    {"!OWNER RELEASE\r", "owner", 0, 2, false},
    {"!SESSION WATCHDOG {\"timeout_ms\":1000}\r", "owner", 0, 2, false},
    {"!SESSION PING\r", "owner", 0, 2, false},
    {"!MOTION STATUS\r", NULL, 0, 2, false},
    {"!ROTOR GET\r", NULL, 0, 2, false},
    {"!ROTOR SET {\"model_id\":5}\r", NULL, 0, 1, false},
    {"!FAULT CLEAR\r", NULL, 0, 2, false}
};

static bool prepare(gs_fixture_t *fixture, const command_case_t *test, char command[160])
{
    if (test->prepare != NULL) {
        if (strcmp(test->prepare, "auth") == 0) {
            char token[65];
            CHECK(management_enable(fixture, token));
            snprintf(command, 160, test->command, token);
            return true;
        }
        CHECK(management_command(fixture, 0,
            strcmp(test->prepare, "owner") == 0 ? "!OWNER TAKE\r" : "!CFG BEGIN\r", GS232B_OK, NULL));
        if (strcmp(test->prepare, "trial") == 0) CHECK(management_command(fixture, 0,
            "!CFG APPLY {\"candidate_id\":1,\"timeout_s\":30}\r", GS232B_OK, NULL));
    }
    strcpy(command, test->command);
    return true;
}

bool management_matrix_tests(void)
{
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const command_case_t *test = &cases[i];
        for (size_t outcome = 0; outcome < 3; ++outcome) {
            gs_fixture_t fixture;
            runtime_t runtime = {0};
            CHECK(setup(&fixture, &runtime));
            fixture.dispatcher.network_config.active_revision = 0;
            char command[160];
            CHECK(prepare(&fixture, test, command));
            if (strcmp(test->prepare == NULL ? "" : test->prepare, "auth") == 0) {
                CHECK(gs_reopen_management(&fixture, 1, GS232B_TRANSPORT_ETHERNET_WS));
            }
            if (outcome == 0) CHECK(management_command(&fixture, test->success_session, command, GS232B_OK, NULL));
            else if (outcome == 1) {
                if (test->expired_ap) {
                    fixture.sim.now_us = fixture.dispatcher.provisioning.status.deadline_us;
                }
                CHECK(management_command(&fixture, test->denied_session, command, GS232B_ERROR_NOT_AUTHORIZED, NULL));
            } else {
                CHECK(gs_feed(&fixture, test->success_session, command) == GS232B_OK);
                const gs232b_fake_store_t before = fixture.store;
                const size_t stopping_session = test->success_session == 1 ? 0 : 1;
                CHECK(gs_feed(&fixture, stopping_session, "S\r") == GS232B_OK);
                CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
                char expected[160];
                gs232b_extension_request_t request;
                CHECK(gs232b_extension_parse((const uint8_t *)command, strlen(command) - 1U, &request) == GS232B_OK);
                snprintf(expected, sizeof(expected), "!{\"ok\":false,\"cmd\":\"%s\",\"error\":\"CONFLICT\"}\r\n",
                    request.cmd);
                CHECK(gs_expect(&fixture, test->success_session, expected));
                CHECK(gs_expect(&fixture, stopping_session, "\r"));
                CHECK(memcmp(&fixture.store, &before, sizeof(before)) == 0);
                CHECK(stopped(&fixture.sim));
            }
        }
    }
    return true;
}
