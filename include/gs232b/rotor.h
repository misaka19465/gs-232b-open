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

#ifndef GS232B_ROTOR_H
#define GS232B_ROTOR_H

#include "gs232b/motion.h"

#define GS232B_ROTOR_DEFAULT_MODEL_ID UINT16_C(1)
#define GS232B_ROTOR_MODEL_COUNT 16U
#define GS232B_PERSISTENCE_SCHEMA_ROTOR 1U

/** @brief 检查数字 ID 是否对应已支持的机型。 */
bool gs232b_rotor_model_valid(uint32_t model_id);

/**
 * @brief 在调用方的独立配置中初始化通用默认值，再应用指定机型的覆盖项。
 * @note 零点始终无效；未知 ID 返回 INVALID_ARGUMENT，并保留两轴禁用的默认值。
 *       config 为 NULL 时只返回 INVALID_ARGUMENT；不保留调用方指针或共享可变配置。
 */
gs232b_error_t gs232b_rotor_config(uint32_t model_id, gs232b_motion_config_t *config);

/**
 * @brief 加载型号与当前记录 revision；仅无记录时使用默认 ID、revision=0。
 * @note 损坏记录可回退上一有效槽；其他错误清零输出，不猜测型号。
 *       回退 payload 时仍保留最新完整记录的 revision，供后续原子修复。
 */
gs232b_error_t gs232b_rotor_load(const gs232b_store_t *store,
    uint16_t *model_id, uint64_t *revision);

/** @brief 比较 revision 后持久化有效数字 ID；不修改运动配置，失败将 new_revision 置零。 */
gs232b_error_t gs232b_rotor_save(const gs232b_store_t *store,
    uint64_t expected_revision, uint16_t model_id, uint64_t *new_revision);

#endif
