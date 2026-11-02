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

#define RESET_LINE "!NET RESET {\"confirm\":\"NETWORK_ONLY\"}\r"

static bool reset_preserves_every_other_domain(void)
{
    gs_fixture_t f; runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    const gs232b_store_t port = gs232b_fake_store_port(&f.store);
    for (gs232b_store_domain_t domain = GS232B_STORE_DISPLAY; domain < GS232B_STORE_DOMAIN_COUNT; ++domain) {
        uint64_t revision = 0;
        const uint8_t payload[] = {12, 34, 56, 78};
        CHECK(gs232b_persistence_commit(&port, domain, 0, 1, payload, sizeof(payload), &revision) == GS232B_OK);
    }
    const gs232b_network_config_t config = {.security = GS232B_NETWORK_SECURITY_OPEN};
    uint64_t revision = 0;
    CHECK(gs232b_persistence_network_commit(&port, 0, &config, &revision) == GS232B_OK);
    CHECK(gs232b_persistence_network_commit(&port, 1, &config, &revision) == GS232B_OK);
    CHECK(f.store.records[GS232B_STORE_NETWORK].previous_present);
    const gs232b_fake_store_t before = f.store;
    const gs232b_motion_config_t mechanical = f.sim.motion.config;
    CHECK(management_command(&f, 0, RESET_LINE, GS232B_OK, "\"configured\":false"));
    CHECK(!f.store.records[GS232B_STORE_NETWORK].present && !f.store.records[GS232B_STORE_NETWORK].previous_present);
    for (gs232b_store_domain_t domain = GS232B_STORE_DISPLAY; domain < GS232B_STORE_DOMAIN_COUNT; ++domain) {
        CHECK(memcmp(&before.records[domain], &f.store.records[domain], sizeof(before.records[domain])) == 0);
    }
    CHECK(memcmp(&f.sim.motion.config, &mechanical, sizeof(mechanical)) == 0);
    CHECK(!f.dispatcher.network_config.active.wifi.enabled && f.dispatcher.network_config.active.wifi.password[0] == '\0');
    CHECK(f.dispatcher.network_config.active.eth.mode == GS232B_ETHERNET_DHCP);
    CHECK(runtime.apply_calls == 1 && !runtime.applied_config.wifi.enabled);
    CHECK(f.dispatcher.provisioning.status.state == GS232B_PROVISIONING_ACTIVE);
    CHECK(!f.dispatcher.provisioning.configured && f.dispatcher.provisioning.status.first_boot);
    return true;
}

static bool reset_conflicts_and_failures(void)
{
    gs_fixture_t f; runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(management_command(&f, 0, "!NET RESET {\"confirm\":\"ALL\"}\r", GS232B_ERROR_INVALID_ARGUMENT, NULL));
    CHECK(management_command(&f, 0, "!CFG BEGIN\r", GS232B_OK, NULL));
    CHECK(management_command(&f, 0, RESET_LINE, GS232B_ERROR_CONFLICT, NULL));
    CHECK(management_command(&f, 0, "!CFG ABORT\r", GS232B_OK, NULL));
    CHECK(gs_run(&f, 0, "W010 180 120 200 130\r", "\r"));
    CHECK(management_command(&f, 0, RESET_LINE, GS232B_ERROR_BUSY, NULL));
    CHECK(gs_run(&f, 0, "S\r", "\r"));
    CHECK(gs232b_fake_fail_next(&f.store.failures[GS232B_STORE_NETWORK][GS232B_FAKE_STORE_ERASE],
        GS232B_ERROR_IO, 1) == GS232B_OK);
    const gs232b_net_config_t active = f.dispatcher.network_config.active;
    CHECK(management_command(&f, 0, RESET_LINE, GS232B_ERROR_IO, NULL));
    CHECK(memcmp(&active, &f.dispatcher.network_config.active, sizeof(active)) == 0 && runtime.apply_calls == 0);
    runtime.apply_failure = GS232B_ERROR_IO;
    runtime.apply_failures_remaining = 3;
    CHECK(management_command(&f, 0, RESET_LINE, GS232B_ERROR_IO, NULL));
    CHECK(f.dispatcher.network_reset_pending && !f.dispatcher.network_config.active.wifi.enabled);
    CHECK(management_command(&f, 0, "!CFG BEGIN\r", GS232B_ERROR_BUSY, NULL));
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(f.dispatcher.network_reset_pending);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(!f.dispatcher.network_reset_pending && runtime.apply_calls == 4);
    return true;
}

int main(void)
{
    return reset_preserves_every_other_domain() && reset_conflicts_and_failures() ? EXIT_SUCCESS : EXIT_FAILURE;
}
