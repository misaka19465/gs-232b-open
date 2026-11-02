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

#ifndef TEST_RMT_API_H
#define TEST_RMT_API_H
#include "driver/rmt_rx.h"
typedef struct {
    rmt_rx_channel_config_t config;
    rmt_receive_config_t receive_config;
    rmt_rx_done_callback_t callback;
    void *ctx;
    rmt_symbol_word_t *buffer;
    size_t buffer_size;
    bool allocated, enabled, receiving, fail_disable, fail_delete, fail_receive;
    bool wrong_buffer;
    bool rx_high;
    int init_calls, fail_init_at;
    unsigned receives, completions;
} fake_rmt_t;
extern fake_rmt_t fake_rmt;
void fake_rmt_reset(void);
void fake_rmt_complete(const rmt_symbol_word_t *symbols, size_t count, bool last);
#endif
