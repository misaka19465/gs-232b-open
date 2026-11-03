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

static bool modes_survive_trial_confirmation(void)
{
    static const char *const names[] = {"auto", "wpa-personal", "wpa2-personal", "wpa3-personal"};
    const gs232b_network_security_t stored_modes[] = {
        GS232B_NETWORK_SECURITY_PERSONAL_AUTO, GS232B_NETWORK_SECURITY_WPA_PERSONAL,
        GS232B_NETWORK_SECURITY_WPA2_PERSONAL, GS232B_NETWORK_SECURITY_WPA3_PERSONAL
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        gs_fixture_t fixture;
        runtime_t runtime = {0};
        CHECK(setup(&fixture, &runtime));
        fixture.dispatcher.network_config.active_revision = 0;
        CHECK(response_contains(&fixture, 0, "!CFG BEGIN\r", "\"ok\":true"));
        char command[160], expected[64];
        snprintf(command, sizeof(command), "!NET SET {\"wifi\":{\"security\":\"%s\"}}\r", names[i]);
        CHECK(response_contains(&fixture, 0, command, "\"ok\":true"));
        CHECK(fixture.dispatcher.network_config.candidate.wifi.security == (gs232b_wifi_sta_security_t)i);
        snprintf(expected, sizeof(expected), "\"security\":\"%s\"", names[i]);
        CHECK(response_contains(&fixture, 0, "!NET GET\r", expected));
        CHECK(response_contains(&fixture, 0, "!CFG CHECK\r", i == 0 ? "\"wifi\":false" : "\"wifi\":true"));
        if (i == 3) {
            CHECK(response_contains(&fixture, 0, "!NET SET {\"wifi\":{\"password\":\"s\"}}\r", "\"ok\":true"));
            const gs232b_config_transaction_t before = fixture.dispatcher.network_config;
            CHECK(rejected(&fixture, 0, "!NET SET {\"wifi\":{\"security\":\"wpa2-personal\"}}\r", GS232B_ERROR_INVALID_ARGUMENT));
            CHECK(memcmp(&before, &fixture.dispatcher.network_config, sizeof(before)) == 0);
        }
        uint32_t trial_id;
        const gs232b_session_key_t session = fixture.sessions[0];
        const gs232b_net_apply_port_t port = {&runtime, runtime_apply};
        CHECK(gs232b_config_trial_apply(&fixture.dispatcher.network_config, session, 1, 60,
            &fixture.dispatcher.store, port, &trial_id) == GS232B_OK);
        CHECK(runtime.applied_config.wifi.security == (gs232b_wifi_sta_security_t)i);
        CHECK(gs232b_config_trial_confirm(&fixture.dispatcher.network_config, trial_id,
            &fixture.dispatcher.store, port) == GS232B_OK);
        gs232b_network_config_t loaded;
        uint64_t revision;
        CHECK(gs232b_persistence_network_load(&fixture.dispatcher.store, &loaded, &revision) == GS232B_OK);
        CHECK(revision == 1 && loaded.security == stored_modes[i]);
        const gs232b_wifi_sta_config_t restored = gs232b_persistence_network_wifi(&loaded);
        CHECK(restored.security == (gs232b_wifi_sta_security_t)i);
        CHECK(strcmp(restored.password, fixture.dispatcher.network_config.active.wifi.password) == 0);
    }
    return true;
}

static bool old_records_keep_wpa2_policy(void)
{
    gs232b_fake_store_t fake;
    gs232b_fake_store_init(&fake);
    const gs232b_store_t store = gs232b_fake_store_port(&fake);
    const gs232b_network_config_t config = {
        .enabled = true, .security = GS232B_NETWORK_SECURITY_WPA2_PERSONAL,
        .ssid = "legacy", .ssid_length = 6, .password = "password", .password_length = 8
    };
    uint64_t revision;
    CHECK(gs232b_persistence_network_commit(&store, 0, &config, &revision) == GS232B_OK);
    gs232b_persisted_record_t record;
    CHECK(gs232b_persistence_load(&store, GS232B_STORE_NETWORK, &record) == GS232B_OK);
    for (uint16_t schema = 1; schema <= 3; ++schema) {
        gs232b_fake_store_init(&fake);
        const size_t offset = schema == 1 ? 21U : 0U;
        const size_t suffix = schema == 1 ? 17U : schema == 2 ? 13U : 0U;
        CHECK(gs232b_persistence_commit(&store, GS232B_STORE_NETWORK, 0, schema,
            record.payload + offset, record.payload_size - offset - suffix, &revision) == GS232B_OK);
        gs232b_network_config_t loaded;
        CHECK(gs232b_persistence_network_load(&store, &loaded, &revision) == GS232B_OK);
        CHECK(loaded.security == GS232B_NETWORK_SECURITY_WPA2_PERSONAL);
        const gs232b_wifi_sta_config_t restored = gs232b_persistence_network_wifi(&loaded);
        CHECK(restored.security == GS232B_WIFI_STA_SECURITY_WPA2_PERSONAL);
        CHECK(strcmp(restored.password, config.password) == 0);
    }
    return true;
}

bool network_security_tests(void)
{
    return modes_survive_trial_confirmation() && old_records_keep_wpa2_policy();
}
