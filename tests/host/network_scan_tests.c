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

typedef struct {
    unsigned calls;
} scan_fixture_t;

static gs232b_error_t scan_one(void *ctx, gs232b_provisioning_ap_t *aps,
    size_t capacity, size_t *count, bool *truncated)
{
    scan_fixture_t *fixture = ctx;
    ++fixture->calls;
    if (capacity == 0) return GS232B_ERROR_INVALID_ARGUMENT;
    memset(aps, 0, sizeof(*aps));
    strcpy(aps[0].ssid, "control\"\\\n");
    aps[0].rssi = -40;
    aps[0].auth = 3;
    *count = 1;
    *truncated = false;
    return GS232B_OK;
}

bool network_scan_tests(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    scan_fixture_t scan = {0};
    f.dispatcher.provisioning.ap_port = (gs232b_provisioning_ap_port_t){
        .ctx = &scan, .scan = scan_one};
    CHECK(response_contains(&f, 0, "!PROV START\r", "\"state\":\"active\""));
    CHECK(rejected(&f, 1, "!PROV SCAN\r", GS232B_ERROR_NOT_AUTHORIZED));
    static const char *const invalid[] = {
        "{\"cursor\":0}", "{\"scan_id\":1}", "{\"scan_id\":0,\"cursor\":0}",
        "{\"scan_id\":1,\"cursor\":-1}", "{\"scan_id\":1,\"cursor\":0.5}",
        "{\"scan_id\":4294967296,\"cursor\":0}", "{\"unknown\":true}"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        char command[128];
        snprintf(command, sizeof(command), "!PROV SCAN %s\r", invalid[i]);
        CHECK(rejected(&f, 0, command, GS232B_ERROR_INVALID_ARGUMENT));
    }
    CHECK(scan.calls == 0);
    CHECK(response_contains(&f, 0, "!PROV SCAN\r", "\"total\":1,\"next\":null"));
    CHECK(scan.calls == 1);
    CHECK(response_contains(&f, 0,
        "!PROV SCAN {\"scan_id\":1,\"cursor\":0}\r", "control\\\"\\\\\\u000a"));
    CHECK(scan.calls == 1);
    CHECK(rejected(&f, 0, "!PROV SCAN {\"scan_id\":1,\"cursor\":2}\r",
        GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(response_contains(&f, 2, "!PROV SCAN\r", "\"scan_id\":2"));
    CHECK(scan.calls == 2);
    CHECK(rejected(&f, 0, "!PROV SCAN {\"scan_id\":1,\"cursor\":0}\r",
        GS232B_ERROR_CONFLICT));
    return true;
}
