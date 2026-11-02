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

#include "gs232b/uart_baud.h"

#include <string.h>

bool gs232b_uart_baud_valid(uint32_t baud)
{
    return baud == 1200 || baud == 2400 || baud == 4800 || baud == 9600;
}

void gs232b_uart_baud_reset(gs232b_uart_baud_detector_t *detector)
{
    if (detector != NULL) { memset(detector, 0, sizeof(*detector)); }
}

static bool width_matches(uint32_t width, uint32_t baud, unsigned bits)
{
    const uint64_t measured = (uint64_t)width * baud;
    const uint64_t nominal = UINT64_C(1000000) * bits;
    return measured >= nominal * 92U / 100U && measured <= nominal * 108U / 100U;
}

static uint32_t start_baud(uint32_t width)
{
    static const uint32_t rates[] = {1200, 2400, 4800, 9600};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        if (width_matches(width, rates[i], 1)) { return rates[i]; }
    }
    return 0;
}

void gs232b_uart_baud_pulse(gs232b_uart_baud_detector_t *detector,
                           bool high, uint32_t duration_us)
{
    if (detector == NULL || detector->baud != 0) { return; }
    /* NOTE: CR 的 start + LSB-first 数据位合并为 L1/H1/L1/H2/L4；
     * 第六段 H 包含停止位和可选空闲，必须真实捕获到足够的高电平。 */
    static const unsigned widths[] = {1, 1, 1, 2, 4};
    if (detector->phase == 0) {
        if (high) {
            if (detector->previous != 0 &&
                (uint64_t)duration_us * detector->previous < UINT64_C(920000)) {
                gs232b_uart_baud_reset(detector);
            }
            return;
        }
        detector->candidate = start_baud(duration_us);
        if (detector->candidate == 0) { gs232b_uart_baud_reset(detector); return; }
        detector->phase = 1;
        return;
    }
    const unsigned phase = detector->phase;
    const bool valid_level = high == ((phase & 1U) != 0);
    const bool valid_width = phase == 5 ?
        (uint64_t)duration_us * detector->candidate >= UINT64_C(920000) :
        width_matches(duration_us, detector->candidate, widths[phase]);
    if (!valid_level || !valid_width) {
        gs232b_uart_baud_reset(detector);
        /* NOTE: 错误低电平也可能是下一完整字符的起始位，但不能继承旧计数。 */
        if (!high && (detector->candidate = start_baud(duration_us)) != 0) {
            detector->phase = 1;
        }
        return;
    }
    if (phase != 5) { ++detector->phase; return; }
    if (detector->previous != detector->candidate) { detector->complete_crs = 0; }
    detector->previous = detector->candidate;
    detector->phase = 0;
    if (++detector->complete_crs == GS232B_UART_BAUD_REQUIRED_CRS) {
        detector->baud = detector->candidate;
    }
}

void gs232b_uart_baud_end_capture(gs232b_uart_baud_detector_t *detector)
{
    if (detector != NULL && detector->baud == 0 && detector->phase != 0) {
        gs232b_uart_baud_reset(detector);
    }
}
