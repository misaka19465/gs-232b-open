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

#include "app_test_support.h"

static bool run(app_fixture_t *f, const char *line, const char *reply)
{
    gs232b_command_context_t context;
    CHECK(gs232b_dispatcher_capture(&f->app.dispatcher, f->uart, &context) == GS232B_OK);
    const gs232b_error_t admitted = gs232b_dispatcher_feed(&f->app.dispatcher, context,
        (const uint8_t *)line, strlen(line));
    CHECK(admitted == GS232B_OK || admitted == GS232B_ERROR_NOT_READY);
    (void)gs232b_app_step(&f->app);
    return expect(f, reply);
}

static bool all_models_boot_with_shared_settings(void)
{
    for (uint16_t id = 1; id <= GS232B_ROTOR_MODEL_COUNT; ++id) {
        app_fixture_t f;
        CHECK(fixture_init(&f, true));
        const gs232b_store_t store = gs232b_fake_store_port(&f.store);
        uint64_t revision = 0;
        CHECK(gs232b_rotor_save(&store, 0, id, &revision) == GS232B_OK);
        CHECK(boot(&f));
        CHECK(f.app.dispatcher.active_model_id == id && f.app.dispatcher.rotor_model_id == id);
        CHECK(f.app.motion.config.model_id == id);
        gs232b_motion_config_t config;
        CHECK(gs232b_rotor_config(id, &config) == GS232B_OK);
        for (unsigned axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
            CHECK(f.app.motion.config.axes[axis].enabled == config.axes[axis].enabled);
            CHECK(f.app.motion.config.axes[axis].zero_valid && f.app.motion.config.axes[axis].zero_uv == 100000);
            CHECK(f.app.motion.state.axes[axis].mode == (config.axes[axis].enabled ? GS232B_AXIS_IDLE : GS232B_AXIS_DISABLED));
        }
        CHECK(run(&f, "!ROTOR GET\r", "\"restart_required\":false"));
        CHECK(f.spi.latched == 0x10 && f.spi.pwm == 0);
        gs232b_fake_spi_bus_destroy(&f.spi);
    }
    return true;
}

static bool deferred_selection_and_reboot(void)
{
    app_fixture_t f;
    CHECK(fixture_init(&f, true) && boot(&f));
    CHECK(f.app.motion.config.model_id == 1 && f.app.dispatcher.rotor_revision == 0);
    CHECK(run(&f, "P36\r", "\r"));
    CHECK(run(&f, "Z\r", "\r"));
    const gs232b_fake_store_t before = f.store;
    CHECK(run(&f, "!ROTOR SET {\"model_id\":5}\r", "\"restart_required\":true"));
    CHECK(f.app.status.phase == GS232B_APP_RUNNING && f.app.dispatcher.active_model_id == 1);
    CHECK(f.app.motion.config.axes[1].enabled);
    CHECK(run(&f, "U\r", "\r"));
    CHECK(run(&f, "S\r", "\r"));
    for (unsigned domain = 0; domain < GS232B_STORE_ROTOR; ++domain) {
        CHECK(memcmp(&f.store.records[domain], &before.records[domain], sizeof(before.records[domain])) == 0);
    }
    const gs232b_fake_store_t saved = f.store;
    gs232b_fake_spi_bus_destroy(&f.spi);
    CHECK(fixture_init(&f, false));
    f.store = saved;
    CHECK(boot(&f));
    CHECK(f.app.dispatcher.active_model_id == 5 && f.app.motion.config.model_id == 5);
    CHECK(!f.app.motion.config.axes[1].enabled);
    CHECK(f.app.dispatcher.display.mode == GS232B_AZIMUTH_DISPLAY_P36);
    CHECK(f.app.dispatcher.display.center == GS232B_AZIMUTH_CENTER_SOUTH);
    CHECK(run(&f, "!ROTOR GET\r", "\"restart_required\":false"));
    CHECK(run(&f, "!SYS INFO\r", "\"azimuth\":true,\"elevation\":false"));
    CHECK(run(&f, "B\r", "EL=000\r\n"));
    CHECK(run(&f, "U\r", "?>\r\n"));
    CHECK(run(&f, "R\r", "\r"));
    CHECK(run(&f, "S\r", "\r"));
    gs232b_fake_spi_bus_destroy(&f.spi);
    return true;
}

static bool elevation_only_native_commands(void)
{
    app_fixture_t f;
    CHECK(fixture_init(&f, true));
    const gs232b_store_t store = gs232b_fake_store_port(&f.store);
    uint64_t revision = 0;
    CHECK(gs232b_rotor_save(&store, 0, 6, &revision) == GS232B_OK && boot(&f));
    CHECK(run(&f, "!SYS INFO\r", "\"azimuth\":false,\"elevation\":true"));
    CHECK(run(&f, "B\r", "EL="));
    CHECK(run(&f, "U\r", "\r"));
    CHECK(run(&f, "E\r", "\r"));
    CHECK(run(&f, "D\r", "\r"));
    CHECK(run(&f, "S\r", "\r"));
    const char *const rejected[] = {"R\r", "L\r", "X1\r", "C\r", "C2\r", "M090\r", "W000 090\r", "O\r", "F\r", "F2\r"};
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        CHECK(run(&f, rejected[i], "?>\r\n"));
        CHECK(f.spi.latched == 0x10 && f.spi.pwm == 0);
    }
    CHECK(run(&f, "O2\r", "ELoffset"));
    CHECK(run(&f, "Y\r", "Completed"));
    CHECK(f.app.motion.config.axes[1].zero_uv == 500000);
    CHECK(f.app.motion.config.axes[0].zero_uv == 100000);
    gs232b_fake_spi_bus_destroy(&f.spi);
    return true;
}

static bool corrupt_model_fails_stopped(void)
{
    app_fixture_t f;
    CHECK(fixture_init(&f, true));
    const gs232b_store_t store = gs232b_fake_store_port(&f.store);
    const uint8_t payload[] = {17, 0};
    uint64_t revision = 0;
    CHECK(gs232b_persistence_commit(&store, GS232B_STORE_ROTOR, 0, 1,
        payload, sizeof(payload), &revision) == GS232B_OK);
    CHECK(start(&f));
    advance(&f, GS232B_W5500_RESET_DEFAULT_US);
    CHECK(gs232b_app_step(&f.app) == GS232B_OK);
    advance(&f, GS232B_W5500_RECOVERY_DEFAULT_US);
    CHECK(gs232b_app_step(&f.app) == GS232B_ERROR_CORRUPT);
    CHECK(f.app.status.phase == GS232B_APP_FAILED && f.app.status.fatal_error == GS232B_ERROR_CORRUPT);
    CHECK(f.spi.latched == 0x10 && f.spi.pwm == 0 && f.uart_opens == 0 && f.network_starts == 0);
    gs232b_fake_spi_bus_destroy(&f.spi);
    return true;
}

int main(void)
{
    return all_models_boot_with_shared_settings() && deferred_selection_and_reboot() &&
        elevation_only_native_commands() && corrupt_model_fails_stopped() ? 0 : 1;
}
