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

#ifndef TEST_NVS_API_H
#define TEST_NVS_API_H

#include "nvs.h"
#include "gs232b/ports.h"

typedef enum {
    FAKE_NVS_SET_BLOB = 0,
    FAKE_NVS_COMMIT_DATA,
    FAKE_NVS_SET_SELECTOR,
    FAKE_NVS_COMMIT_SELECTOR,
    FAKE_NVS_ERASE,
    FAKE_NVS_COMMIT_ERASE,
    FAKE_NVS_STAGE_COUNT
} fake_nvs_stage_t;

void fake_nvs_reset(void);
void fake_nvs_fail(fake_nvs_stage_t stage, gs232b_error_t error);

#endif
