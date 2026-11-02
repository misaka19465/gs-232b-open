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

#include "gs232b/nvs_store.h"
#include "nvs_api.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int expect_record(gs232b_store_t store, const uint8_t *expected, size_t expected_size)
{
    uint8_t bytes[16];
    size_t size = 0;
    if (gs232b_store_load(&store, GS232B_STORE_DISPLAY, bytes, sizeof(bytes), &size) != GS232B_OK ||
        size != expected_size || memcmp(bytes, expected, size) != 0) return 1;
    return 0;
}

int main(void)
{
    static const uint8_t original[] = {1, 2, 3, 4};
    static const uint8_t replacement[] = {8, 9};
    static const fake_nvs_stage_t failures[] = {
        FAKE_NVS_SET_BLOB, FAKE_NVS_COMMIT_DATA,
        FAKE_NVS_SET_SELECTOR, FAKE_NVS_COMMIT_SELECTOR
    };
    fake_nvs_reset();
    gs232b_nvs_store_t nvs = {0};
    CHECK(gs232b_nvs_store_open(&nvs, "gs232b") == GS232B_OK);
    const gs232b_store_t store = gs232b_nvs_store_port(&nvs);
    CHECK(gs232b_store_save(&store, GS232B_STORE_DISPLAY, original, sizeof(original)) == GS232B_OK);
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        fake_nvs_fail(failures[i], GS232B_ERROR_IO);
        CHECK(gs232b_store_save(&store, GS232B_STORE_DISPLAY, replacement, sizeof(replacement)) ==
            GS232B_ERROR_IO);
        CHECK(expect_record(store, original, sizeof(original)) == 0);
        gs232b_nvs_store_close(&nvs);
        CHECK(gs232b_nvs_store_open(&nvs, "gs232b") == GS232B_OK);
        CHECK(expect_record(store, original, sizeof(original)) == 0);
        uint8_t previous[16];
        size_t size = 0;
        const gs232b_error_t previous_error = gs232b_store_load_previous(&store,
            GS232B_STORE_DISPLAY, previous, sizeof(previous), &size);
        if (i < 2) CHECK(previous_error == GS232B_ERROR_NOT_FOUND);
        else CHECK(previous_error == GS232B_OK && size == sizeof(replacement) &&
            memcmp(previous, replacement, size) == 0);
    }
    CHECK(gs232b_store_save(&store, GS232B_STORE_DISPLAY, replacement, sizeof(replacement)) == GS232B_OK);
    uint8_t previous[16];
    size_t size = 0;
    CHECK(gs232b_store_load_previous(&store, GS232B_STORE_DISPLAY, previous,
        sizeof(previous), &size) == GS232B_OK);
    CHECK(size == sizeof(original) && memcmp(previous, original, size) == 0);
    const fake_nvs_stage_t erase_failures[] = {FAKE_NVS_ERASE, FAKE_NVS_COMMIT_ERASE};
    for (size_t i = 0; i < sizeof(erase_failures) / sizeof(erase_failures[0]); ++i) {
        fake_nvs_fail(erase_failures[i], GS232B_ERROR_IO);
        CHECK(gs232b_store_erase(&store, GS232B_STORE_DISPLAY) == GS232B_ERROR_IO);
        CHECK(expect_record(store, replacement, sizeof(replacement)) == 0);
    }
    CHECK(gs232b_store_erase(&store, GS232B_STORE_DISPLAY) == GS232B_OK);
    CHECK(gs232b_store_load(&store, GS232B_STORE_DISPLAY, previous, sizeof(previous), &size) ==
        GS232B_ERROR_NOT_FOUND);
    CHECK(gs232b_store_load_previous(&store, GS232B_STORE_DISPLAY, previous, sizeof(previous), &size) ==
        GS232B_ERROR_NOT_FOUND);
    gs232b_nvs_store_close(&nvs);
    puts("NVS blob, data commit, selector commit and erase failures preserve active records");
    return 0;
}
