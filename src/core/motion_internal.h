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

#ifndef GS232B_MOTION_INTERNAL_H
#define GS232B_MOTION_INTERNAL_H

#include "gs232b/motion.h"

/* NOTE: 本头仅供运动模块内部协作；所有函数遵守 motion.h 的串行所有权。 */
/** @brief 检查对象及借用端口是否已经初始化。 */
bool gs232b_motion_valid(const gs232b_motion_t *motion);
/** @brief 检查配置能力，不代表反馈有效；axis 必须在 0..AXIS_COUNT-1 内。 */
bool gs232b_motion_axis_ready(const gs232b_motion_t *motion, int axis);
/** @brief 检查两轴是否有动作意图，包含尚未使能输出的换向等待。 */
bool gs232b_motion_active(const gs232b_motion_t *motion);
/** @brief 清动作、目标和监测方向；调用者须继续提交停机或故障处理。 */
void gs232b_motion_cancel_axis(gs232b_motion_t *motion, int axis);
/** @brief 同步实际输出、模式和方向监测基准，不推进输出或采样。 */
void gs232b_motion_sync_output(gs232b_motion_t *motion);
/** @brief 读取启用轴并刷新原始/滤波位置和错误；不提交输出或锁定故障。 */
void gs232b_motion_refresh_feedback(gs232b_motion_t *motion);
/** @brief 返回反馈/限位故障；require_sample=false 时允许普通未就绪采样。 */
gs232b_fault_code_t gs232b_motion_feedback_fault(const gs232b_motion_t *motion,
                                                int axis, bool require_sample);
/** @brief 首次故障撤销两轴并尝试全停，保留首次诊断，返回本次 error。 */
gs232b_error_t gs232b_motion_latch_fault(gs232b_motion_t *motion,
                                        gs232b_fault_code_t code,
                                        gs232b_error_t error, uint8_t axis_mask);
/** @brief 基于已检查的新鲜反馈提交两轴动作；调用者须先排除 FAULT。 */
gs232b_error_t gs232b_motion_drive(gs232b_motion_t *motion);

#endif
