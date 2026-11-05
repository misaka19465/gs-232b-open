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

#include "gs232b/rotor.h"
#include "gs232b/persistence.h"
#include "gs232b/fakes.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return false; } } while (0)

static bool independent_configs(void)
{
    const uint8_t masks[] = {3, 3, 3, 3, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 3, 3};
    CHECK(GS232B_ROTOR_DEFAULT_MODEL_ID == 1 && GS232B_ROTOR_MODEL_COUNT == sizeof(masks));
    CHECK(GS232B_STORE_ADMIN == 6 && GS232B_STORE_ROTOR == 7);
    CHECK(!gs232b_rotor_model_valid(0) && !gs232b_rotor_model_valid(17));
    CHECK(!gs232b_rotor_model_valid(UINT32_MAX));
    gs232b_motion_config_t defaults;
    gs232b_motion_config_init(&defaults);
    gs232b_motion_config_t configs[GS232B_ROTOR_MODEL_COUNT];
    for (uint32_t id = 1; id <= GS232B_ROTOR_MODEL_COUNT; ++id) {
        gs232b_motion_config_t *config = &configs[id - 1U];
        CHECK(gs232b_rotor_model_valid(id));
        CHECK(gs232b_rotor_config(id, config) == GS232B_OK && config->model_id == id);
        CHECK(config->azimuth_speed_permille == defaults.azimuth_speed_permille);
        CHECK(config->azimuth_slow_speed_permille == defaults.azimuth_slow_speed_permille);
        CHECK(gs232b_speed_levels_valid(&config->azimuth_speed_levels));
        for (unsigned axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
            const gs232b_motion_axis_config_t *a = &config->axes[axis], *b = &defaults.axes[axis];
            CHECK(a->enabled == ((masks[id - 1U] & (1U << axis)) != 0));
            CHECK(!a->zero_valid && a->zero_uv == 0);
            CHECK(a->minimum_mdeg == b->minimum_mdeg && a->maximum_mdeg == b->maximum_mdeg);
            CHECK(a->tolerance_mdeg == b->tolerance_mdeg && a->slowdown_mdeg == b->slowdown_mdeg);
        }
    }
    configs[0].axes[0].tolerance_mdeg = 1000;
    configs[0].axes[0].zero_valid = true;
    configs[0].azimuth_speed_levels.permille[0] = 100;
    CHECK(configs[1].axes[0].tolerance_mdeg == defaults.axes[0].tolerance_mdeg);
    CHECK(configs[1].azimuth_speed_levels.permille[0] == defaults.azimuth_speed_levels.permille[0]);
    CHECK(gs232b_rotor_config(1, &configs[0]) == GS232B_OK);
    CHECK(!configs[0].axes[0].zero_valid && configs[0].axes[0].tolerance_mdeg == 500);
    CHECK(configs[0].azimuth_speed_levels.permille[0] == defaults.azimuth_speed_levels.permille[0]);
    const uint32_t invalid[] = {0, 17, UINT32_MAX};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(gs232b_rotor_config(invalid[i], &configs[0]) == GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(configs[0].model_id == 0 && !configs[0].axes[0].enabled && !configs[0].axes[1].enabled);
        CHECK(configs[0].axes[0].tolerance_mdeg == defaults.axes[0].tolerance_mdeg);
    }
    CHECK(gs232b_rotor_config(1, NULL) == GS232B_ERROR_INVALID_ARGUMENT);
    return true;
}

static bool persistence_and_isolation(void)
{
    gs232b_fake_store_t fake;
    gs232b_fake_store_init(&fake);
    const gs232b_store_t store = gs232b_fake_store_port(&fake);
    uint16_t id = 99;
    uint64_t revision = 99, loaded_revision = 99;
    CHECK(gs232b_rotor_load(&store, &id, &revision) == GS232B_OK && id == 1 && revision == 0);
    for (uint16_t next = 1; next <= GS232B_ROTOR_MODEL_COUNT; ++next) {
        CHECK(gs232b_rotor_save(&store, revision, next, &revision) == GS232B_OK);
        CHECK(gs232b_rotor_load(&store, &id, &loaded_revision) == GS232B_OK);
        CHECK(id == next && loaded_revision == revision && revision == next);
        gs232b_persisted_record_t record;
        CHECK(gs232b_persistence_load(&store, GS232B_STORE_ROTOR, &record) == GS232B_OK);
        CHECK(record.schema_version == GS232B_PERSISTENCE_SCHEMA_ROTOR && record.payload_size == 2);
        CHECK(record.payload[0] == next && record.payload[1] == 0);
    }
    for (unsigned domain = 0; domain < GS232B_STORE_ROTOR; ++domain) CHECK(!fake.records[domain].present);
    CHECK(gs232b_rotor_save(&store, 0, 5, &loaded_revision) == GS232B_ERROR_CONFLICT && loaded_revision == 0);
    CHECK(gs232b_rotor_save(&store, revision, 0, &loaded_revision) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_rotor_save(&store, revision, 17, &loaded_revision) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_fake_fail_next(&fake.failures[GS232B_STORE_ROTOR][GS232B_FAKE_STORE_SAVE], GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs232b_rotor_save(&store, revision, 5, &loaded_revision) == GS232B_ERROR_IO && loaded_revision == 0);
    CHECK(gs232b_fake_fail_next(&fake.failures[GS232B_STORE_ROTOR][GS232B_FAKE_STORE_INTERRUPT], GS232B_ERROR_TIMEOUT, 1) == GS232B_OK);
    CHECK(gs232b_rotor_save(&store, revision, 5, &loaded_revision) == GS232B_ERROR_TIMEOUT);
    CHECK(gs232b_rotor_load(&store, &id, &loaded_revision) == GS232B_OK && id == 16 && loaded_revision == revision);
    CHECK(gs232b_store_erase(&store, GS232B_STORE_NETWORK) == GS232B_OK);
    CHECK(gs232b_rotor_load(&store, &id, &loaded_revision) == GS232B_OK && id == 16);
    return true;
}

static bool corrupt_records(void)
{
    for (unsigned bad = 0; bad < 4; ++bad) {
        gs232b_fake_store_t fake;
        gs232b_fake_store_init(&fake);
        const gs232b_store_t store = gs232b_fake_store_port(&fake);
        uint64_t revision = 0;
        uint16_t id = 0;
        const uint8_t payload[] = {bad == 0 ? 0 : bad == 1 ? 17 : 5, 0, 0};
        CHECK(gs232b_persistence_commit(&store, GS232B_STORE_ROTOR, 0,
            bad == 2 ? 2 : 1, payload, bad == 3 ? 3 : 2, &revision) == GS232B_OK);
        CHECK(gs232b_rotor_load(&store, &id, &revision) == GS232B_ERROR_CORRUPT && id == 0 && revision == 0);
    }
    gs232b_fake_store_t fake;
    gs232b_fake_store_init(&fake);
    const gs232b_store_t store = gs232b_fake_store_port(&fake);
    uint64_t revision = 0;
    uint16_t id = 0;
    CHECK(gs232b_rotor_save(&store, 0, 5, &revision) == GS232B_OK);
    const uint8_t bad[] = {17, 0};
    CHECK(gs232b_persistence_commit(&store, GS232B_STORE_ROTOR, revision, 1, bad, sizeof(bad), &revision) == GS232B_OK);
    CHECK(gs232b_rotor_load(&store, &id, &revision) == GS232B_OK && id == 5 && revision == 2);
    CHECK(gs232b_rotor_save(&store, revision, 6, &revision) == GS232B_OK && revision == 3);
    CHECK(gs232b_rotor_load(&store, &id, &revision) == GS232B_OK && id == 6);
    CHECK(gs232b_rotor_save(&store, revision, 7, &revision) == GS232B_OK);
    fake.records[GS232B_STORE_ROTOR].bytes[0] ^= 1U;
    CHECK(gs232b_rotor_load(&store, &id, &revision) == GS232B_OK && id == 6 && revision == 3);
    fake.records[GS232B_STORE_ROTOR].previous_bytes[0] ^= 1U;
    CHECK(gs232b_rotor_load(&store, &id, &revision) == GS232B_ERROR_CORRUPT && id == 0 && revision == 0);
    CHECK(gs232b_fake_fail_next(&fake.failures[GS232B_STORE_ROTOR][GS232B_FAKE_STORE_LOAD], GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs232b_rotor_load(&store, &id, &revision) == GS232B_ERROR_IO && id == 0);
    return true;
}

int main(void)
{
    return independent_configs() && persistence_and_isolation() && corrupt_records() ? 0 : 1;
}
