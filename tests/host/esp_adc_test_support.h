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

#ifndef GS232B_ESP_ADC_TEST_SUPPORT_H
#define GS232B_ESP_ADC_TEST_SUPPORT_H

#include "gs232b/esp_adc.h"
#include "gs232b/feedback.h"
#include "adc_api.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static inline bool expect_not_ready(gs232b_sampler_t sampler, gs232b_axis_id_t axis)
{
    gs232b_sample_t sample;
    memset(&sample, 0xa5, sizeof(sample));
    CHECK(gs232b_sampler_read(&sampler, axis, &sample) == GS232B_ERROR_NOT_READY);
    CHECK(!sample.valid && sample.raw == 0 && sample.adc_uv == 0 &&
          sample.acquired_at_us == 0);
    return true;
}

#endif
