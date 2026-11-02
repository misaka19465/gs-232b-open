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

#ifndef GS232B_NETWORK_TEST_SUPPORT_H
#define GS232B_NETWORK_TEST_SUPPORT_H

#include "gs_test_support.h"

typedef struct {
    gs232b_ethernet_snapshot_t eth;
    gs232b_wifi_sta_snapshot_t wifi;
    gs232b_net_config_t applied_config;
    size_t apply_calls;
    size_t apply_failures_remaining;
    gs232b_error_t apply_failure;
    bool mutate_before_apply_failure;
} runtime_t;

static inline gs232b_error_t runtime_snapshot(void *ctx, gs232b_ethernet_snapshot_t *eth,
    gs232b_wifi_sta_snapshot_t *wifi)
{
    runtime_t *runtime = ctx;
    *eth = runtime->eth;
    *wifi = runtime->wifi;
    return GS232B_OK;
}

static inline gs232b_error_t runtime_apply(void *ctx, const gs232b_net_config_t *config)
{
    runtime_t *runtime = ctx;
    ++runtime->apply_calls;
    if (runtime->apply_failures_remaining != 0) {
        --runtime->apply_failures_remaining;
        if (runtime->mutate_before_apply_failure) runtime->applied_config = *config;
        return runtime->apply_failure;
    }
    runtime->applied_config = *config;
    return GS232B_OK;
}

static inline bool setup(gs_fixture_t *fixture, runtime_t *runtime)
{
    memset(fixture, 0, sizeof(*fixture));
    gs232b_motion_config_t motion_config;
    gs232b_motion_sim_config(&motion_config);
    CHECK(gs232b_motion_sim_init(&fixture->sim, &motion_config, 0, WAIT_US) == GS232B_OK);
    gs232b_fake_store_init(&fixture->store);
    CHECK(gs232b_fake_sessions_init(&fixture->io, fixture->sim.manager.clock) == GS232B_OK);
    CHECK(gs232b_dispatcher_init(&fixture->dispatcher, &fixture->sim.motion,
        gs232b_fake_store_port(&fixture->store), gs232b_fake_sessions_port(&fixture->io),
        fixture->request_bytes, sizeof(fixture->request_bytes)) == GS232B_OK);
    const gs232b_net_config_t active = {
        .eth = {.mode = GS232B_ETHERNET_DHCP},
        .wifi = {.enabled = true, .ssid = "home", .password = "prior-secret"}
    };
    const gs232b_net_status_port_t port = {.ctx = runtime, .snapshot = runtime_snapshot};
    CHECK(gs232b_dispatcher_network_configure(&fixture->dispatcher, &active, 7, port) == GS232B_OK);
    CHECK(gs232b_dispatcher_network_apply_configure(&fixture->dispatcher,
        (gs232b_net_apply_port_t){.ctx = runtime, .apply = runtime_apply}) == GS232B_OK);
    CHECK(gs232b_provisioning_start(&fixture->dispatcher.provisioning, 0) == GS232B_OK);
    runtime->eth.state = GS232B_ETHERNET_READY;
    runtime->eth.link_up = true;
    runtime->eth.ipv4_valid = true;
    runtime->eth.current.address = UINT32_C(0xc000020a);
    runtime->eth.current.netmask = UINT32_C(0xffffff00);
    runtime->wifi.state = GS232B_WIFI_STA_READY;
    runtime->wifi.enabled = true;
    runtime->wifi.connected = true;
    runtime->wifi.ipv4_valid = true;
    runtime->wifi.current.address = UINT32_C(0xc6336402);
    for (size_t i = 0; i < GS_TEST_SESSIONS; ++i) {
        CHECK(gs_open(fixture, i, i == 2 ? GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS : (gs232b_transport_t)i));
    }
    return true;
}

static inline bool response_contains(gs_fixture_t *fixture, size_t session,
    const char *line, const char *expected)
{
    CHECK(gs_feed(fixture, session, line) == GS232B_OK);
    const gs232b_error_t tick_error = gs232b_dispatcher_tick(&fixture->dispatcher);
    if (tick_error != GS232B_OK) fprintf(stderr, "dispatcher tick failed: %s\n", gs232b_error_name(tick_error));
    CHECK(tick_error == GS232B_OK);
    uint8_t bytes[4096];
    size_t size;
    CHECK(gs232b_fake_sessions_take_tx(&fixture->io, fixture->sessions[session],
        bytes, sizeof(bytes) - 1U, &size) == GS232B_OK);
    bytes[size] = '\0';
    CHECK(strstr((const char *)bytes, "\"password\":") == NULL);
    CHECK(strstr((const char *)bytes, "prior-secret") == NULL);
    CHECK(strstr((const char *)bytes, "new-secret") == NULL);
    gs232b_json_value_t json;
    CHECK(size > 3 && bytes[0] == '!' && bytes[size - 2U] == '\r' && bytes[size - 1U] == '\n');
    CHECK(gs232b_json_parse((gs232b_byte_span_t){bytes + 1U, size - 3U}, &json) == GS232B_OK);
    if (strstr((const char *)bytes, expected) == NULL) {
        fprintf(stderr, "response did not contain [%s]: %s\n", expected, bytes);
        CHECK(false);
    }
    return true;
}

static inline bool rejected(gs_fixture_t *fixture, size_t session, const char *line,
    gs232b_error_t error)
{
    const gs232b_error_t feed_error = gs_feed(fixture, session, line);
    if (feed_error == GS232B_OK) CHECK(gs232b_dispatcher_tick(&fixture->dispatcher) == error);
    else CHECK(feed_error == error);
    uint8_t bytes[512];
    size_t size;
    CHECK(gs232b_fake_sessions_take_tx(&fixture->io, fixture->sessions[session],
        bytes, sizeof(bytes) - 1U, &size) == GS232B_OK);
    bytes[size] = '\0';
    char expected[64];
    snprintf(expected, sizeof(expected), "\"error\":\"%s\"", gs232b_error_name(error));
    CHECK(strstr((const char *)bytes, expected) != NULL);
    return true;
}


#endif
