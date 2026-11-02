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

#ifndef TEST_LEDC_API_H
#define TEST_LEDC_API_H

#include "driver/ledc.h"

typedef enum {
    LEDC_FAKE_TIMER_OPEN,
    LEDC_FAKE_CHANNEL_OPEN,
    LEDC_FAKE_SET,
    LEDC_FAKE_UPDATE,
    LEDC_FAKE_STOP,
    LEDC_FAKE_CHANNEL_CLOSE,
    LEDC_FAKE_PAUSE,
    LEDC_FAKE_TIMER_CLOSE,
    LEDC_FAKE_OP_COUNT
} ledc_fake_op_t;

typedef struct {
    ledc_timer_config_t timer;
    ledc_channel_config_t channel;
    unsigned calls[LEDC_FAKE_OP_COUNT];
    esp_err_t fail_next[LEDC_FAKE_OP_COUNT];
    uint32_t pending_duty;
    uint32_t submitted_duty;
    bool timer_open;
    bool channel_open;
    bool paused;
    bool invalid_call;
} esp_ledc_fake_t;

extern esp_ledc_fake_t esp_ledc_fake;
void esp_ledc_fake_reset(void);

#endif
