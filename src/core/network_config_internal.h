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

#ifndef GS232B_NETWORK_CONFIG_INTERNAL_H
#define GS232B_NETWORK_CONFIG_INTERNAL_H

#include "gs232b/extension.h"
#include "gs232b/network_config.h"

extern const gs232b_json_schema_t gs232b_net_patch_schema;

/** @brief 在临时副本上合并、校验字段；允许尚缺字段的候选，失败不修改输出。 */
gs232b_error_t gs232b_net_config_patch(const gs232b_net_config_t *base,
    gs232b_json_value_t arguments, gs232b_net_config_t *candidate);

/** @brief 编码 NET GET 的已确认、脱敏候选及可选的实际运行状态。 */
gs232b_error_t gs232b_net_config_encode(const gs232b_config_transaction_t *transaction,
    const gs232b_net_status_port_t *status, uint8_t *data, size_t capacity, size_t *size);
/** @brief 编码 CFG STATUS 的 ID、revision 及有界空闲期限，不续期。 */
gs232b_error_t gs232b_config_status_encode(const gs232b_config_transaction_t *transaction,
    uint8_t *data, size_t capacity, size_t *size);

#endif
