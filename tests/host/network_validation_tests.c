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

static bool test_invalid_fields_are_atomic(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":1"));
    CHECK(response_contains(&f, 0,
        "!NET SET {\"eth\":{\"mode\":\"static\",\"address\":\"192.168.10.50\",\"netmask\":\"255.255.255.0\",\"gateway\":\"192.168.10.1\"}}\r",
        "\"ok\":true"));
    const gs232b_config_transaction_t before = f.dispatcher.network_config;
    static const char *const invalid[] = {
        "{\"eth\":{\"unknown\":1}}", "{\"unknown\":{}}", "{\"eth\":null}",
        "{\"eth\":{\"mode\":\"auto\"}}", "{\"eth\":{\"mode\":true}}",
        "{\"eth\":{\"mode\":\"static\",\"mode\":\"dhcp\"}}",
        "{\"eth\":{\"address\":\"192.168.10\"}}", "{\"eth\":{\"address\":\"192.168.010.50\"}}",
        "{\"eth\":{\"address\":\"256.168.10.50\"}}", "{\"eth\":{\"address\":\"192.168.10.50 \"}}",
        "{\"eth\":{\"address\":\"192.168.10.50\\u0000\"}}",
        "{\"eth\":{\"address\":\"0.0.0.0\"}}", "{\"eth\":{\"address\":\"127.1.1.1\"}}",
        "{\"eth\":{\"address\":\"224.1.1.1\"}}", "{\"eth\":{\"address\":\"240.1.1.1\"}}",
        "{\"eth\":{\"address\":\"192.168.10.0\"}}", "{\"eth\":{\"address\":\"192.168.10.255\"}}",
        "{\"eth\":{\"netmask\":\"0.0.0.0\"}}", "{\"eth\":{\"netmask\":\"255.0.255.0\"}}",
        "{\"eth\":{\"netmask\":\"255.255.255.254\"}}", "{\"eth\":{\"netmask\":\"255.255.255.255\"}}",
        "{\"eth\":{\"gateway\":\"192.168.11.1\"}}", "{\"eth\":{\"gateway\":\"192.168.10.50\"}}",
        "{\"eth\":{\"gateway\":\"192.168.10.0\"}}", "{\"eth\":{\"gateway\":\"192.168.10.255\"}}",
        "{\"eth\":{\"gateway\":\"127.0.0.1\"}}", "{\"eth\":{\"dns1\":\"224.0.0.1\"}}",
        "{\"eth\":{\"dns2\":\"255.255.255.255\"}}", "{\"eth\":{\"dns2\":\"127.0.0.1\"}}",
        "{\"wifi\":{\"security\":\"open\"}}", "{\"wifi\":{\"security\":\"wpa3-enterprise\"}}",
        "{\"wifi\":{\"security\":\"wpa2-enterprise\"}}", "{\"wifi\":{\"security\":\"wep\"}}",
        "{\"wifi\":{\"security\":\"owe\"}}", "{\"wifi\":{\"security\":\"unknown\"}}",
        "{\"wifi\":{\"enabled\":1}}", "{\"wifi\":{\"password_set\":true}}",
        "{\"wifi\":{\"password\":null}}", "{\"wifi\":{\"ssid\":\"x\\u0000y\"}}",
        "{\"wifi\":{\"password\":\"1234567\"}}", "{\"wifi\":{\"password\":\"12345678\\n\"}}",
        "{\"wifi\":{\"password\":\"12345678\\u007f\"}}", "{\"wifi\":{\"password\":\"12345678\\u0000\"}}",
        "{\"wifi\":{\"password\":\"中文中文中文中文\"}}", "{\"wifi\":{\"password\":\"<redacted>\"}}",
        "{\"wifi\":{\"password\":\"REDACTED\"}}", "{\"wifi\":{\"password\":\"********\"}}",
        "{\"eth\":{\"address\":\"192.168.10.51\"},\"wifi\":{\"password\":\"bad\"}}"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        char line[256];
        snprintf(line, sizeof(line), "!NET SET %s\r", invalid[i]);
        ++f.sim.now_us;
        CHECK(rejected(&f, 0, line, GS232B_ERROR_INVALID_ARGUMENT));
        CHECK(memcmp(&f.dispatcher.network_config, &before, sizeof(before)) == 0);
    }
    CHECK(!f.store.records[GS232B_STORE_NETWORK].present);
    return true;
}

static bool test_ssid_bytes_and_password_forms(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":1"));
    CHECK(response_contains(&f, 0,
        "!NET SET {\"wifi\":{\"ssid\":\"中中中中中中中中中中AB\"}}\r", "\"ok\":true"));
    CHECK(strlen(f.dispatcher.network_config.candidate.wifi.ssid) == 32);
    CHECK(response_contains(&f, 0,
        "!NET SET {\"wifi\":{\"ssid\":\"\\u4e2d\\u4e2d\\u4e2d\\u4e2d\\u4e2d\\u4e2d\\u4e2d\\u4e2d\\u4e2d\\u4e2dAB\"}}\r", "\"ok\":true"));
    CHECK(rejected(&f, 0, "!NET SET {\"wifi\":{\"ssid\":\"中中中中中中中中中中中\"}}\r",
        GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(rejected(&f, 0, "!NET SET {\"wifi\":{\"ssid\":\"\300\257\"}}\r",
        GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(strlen(f.dispatcher.network_config.candidate.wifi.ssid) == 32);

    char password[66], line[200];
    memset(password, 'p', 63); password[63] = '\0';
    snprintf(line, sizeof(line), "!NET SET {\"wifi\":{\"password\":\"%s\"}}\r", password);
    CHECK(response_contains(&f, 0, line, "\"ok\":true"));
    memset(password, 'a', 64); password[64] = '\0'; password[0] = 'F';
    snprintf(line, sizeof(line), "!NET SET {\"wifi\":{\"password\":\"%s\"}}\r", password);
    CHECK(response_contains(&f, 0, line, "\"ok\":true"));
    CHECK(strcmp(f.dispatcher.network_config.candidate.wifi.password, password) == 0);
    password[0] = 'g';
    snprintf(line, sizeof(line), "!NET SET {\"wifi\":{\"password\":\"%s\"}}\r", password);
    CHECK(rejected(&f, 0, line, GS232B_ERROR_INVALID_ARGUMENT));
    memset(password, 'a', 65); password[65] = '\0';
    snprintf(line, sizeof(line), "!NET SET {\"wifi\":{\"password\":\"%s\"}}\r", password);
    CHECK(rejected(&f, 0, line, GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(response_contains(&f, 0, "!NET SET {\"wifi\":{\"password\":\" pass\\\"\\\\ word \"}}\r", "\"ok\":true"));
    CHECK(strcmp(f.dispatcher.network_config.candidate.wifi.password, " pass\"\\ word ") == 0);
    CHECK(response_contains(&f, 0, "!NET SET {\"wifi\":{\"ssid\":\"quoted\\\"\\\\name\",\"enabled\":false}}\r", "\"ok\":true"));
    CHECK(response_contains(&f, 0, "!NET GET\r", "quoted\\\"\\\\name"));
    CHECK(response_contains(&f, 0, "!CFG CHECK\r", "\"valid\":true"));
    CHECK(response_contains(&f, 0, "!NET SET {}\r", "\"ok\":true"));
    CHECK(strcmp(f.dispatcher.network_config.candidate.wifi.password, " pass\"\\ word ") == 0);
    return true;
}

static bool test_incremental_candidate_and_dhcp_preservation(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":1"));
    CHECK(response_contains(&f, 0, "!NET SET {\"eth\":{\"mode\":\"static\"}}\r", "\"ok\":true"));
    CHECK(rejected(&f, 0, "!CFG CHECK\r", GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(response_contains(&f, 0, "!NET SET {\"eth\":{\"address\":\"192.168.10.50\"}}\r", "\"ok\":true"));
    CHECK(rejected(&f, 0, "!CFG CHECK\r", GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(response_contains(&f, 0, "!NET SET {\"eth\":{\"netmask\":\"255.255.255.0\"}}\r", "\"ok\":true"));
    CHECK(response_contains(&f, 0, "!CFG CHECK\r", "\"eth\":true,\"wifi\":false"));
    const gs232b_ethernet_ipv4_t static_ip = f.dispatcher.network_config.candidate.eth.ipv4;
    CHECK(response_contains(&f, 0, "!NET SET {\"eth\":{\"mode\":\"dhcp\"}}\r", "\"ok\":true"));
    CHECK(memcmp(&f.dispatcher.network_config.candidate.eth.ipv4, &static_ip, sizeof(static_ip)) == 0);
    CHECK(response_contains(&f, 0, "!CFG CHECK\r", "\"eth\":false,\"wifi\":false"));
    CHECK(response_contains(&f, 0, "!NET SET {\"wifi\":{\"enabled\":false,\"ssid\":\"\",\"password\":\"\"}}\r", "\"ok\":true"));
    CHECK(response_contains(&f, 0, "!CFG CHECK\r", "\"valid\":true"));
    CHECK(response_contains(&f, 0, "!NET SET {\"wifi\":{\"enabled\":true}}\r", "\"ok\":true"));
    CHECK(rejected(&f, 0, "!CFG CHECK\r", GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(response_contains(&f, 0, "!NET SET {\"wifi\":{\"ssid\":\"restored\"}}\r", "\"ok\":true"));
    CHECK(response_contains(&f, 0, "!CFG CHECK\r", "\"valid\":true"));
    CHECK(response_contains(&f, 0, "!NET SET {\"wifi\":{\"password\":\"12345678\"}}\r", "\"ok\":true"));
    CHECK(response_contains(&f, 0, "!CFG CHECK\r", "\"valid\":true"));
    return true;
}

bool network_validation_tests(void)
{
    return test_invalid_fields_are_atomic() && test_ssid_bytes_and_password_forms() &&
        test_incremental_candidate_and_dhcp_preservation();
}
