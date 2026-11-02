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

#ifndef GS232B_CALIBRATION_H
#define GS232B_CALIBRATION_H

#include "gs232b/motion.h"

#define GS232B_CALIBRATION_TIMEOUT_US UINT64_C(60000000)
#define GS232B_CALIBRATION_DISPLAY_PERIOD_US UINT64_C(500000)

/** @brief 校准借用的真实运动核心和分域存储，生命周期覆盖控制核心。 */
typedef struct {
    gs232b_motion_t *motion;
    gs232b_store_t store;
} gs232b_calibration_ports_t;

/* NOTE: 私有布局仅供静态分配；交互属于完整会话键，不能由外部修改。 */
typedef struct {
    gs232b_interaction_t kind;
    gs232b_session_key_t session;
    gs232b_time_us_t started_at_us;
    gs232b_time_us_t checked_at_us;
    gs232b_time_us_t displayed_at_us;
} gs232b_calibration_t;

#endif
