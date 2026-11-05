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

#ifndef GS232B_MANAGEMENT_CRYPTO_H
#define GS232B_MANAGEMENT_CRYPTO_H

#include "gs232b/types.h"
#include "gs232b/persistence.h"
#include <stddef.h>

/** @brief 使用平台 CSPRNG 生成随机盐或其他短期随机材料。 */
gs232b_error_t gs232b_management_random(uint8_t *bytes, size_t size);
/** @brief 使用 20000 轮 PBKDF2-HMAC-SHA-256 派生管理口令校验值。 */
gs232b_error_t gs232b_management_derive_password(const uint8_t *password, size_t password_size,
    const uint8_t salt[GS232B_ADMIN_SALT_SIZE], uint8_t verifier[GS232B_ADMIN_VERIFIER_SIZE]);
/** @brief 清除临时凭据，禁止优化器省略擦除。 */
void gs232b_management_clear(void *bytes, size_t size);
/** @brief 返回实际固件构建版本；宿主测试使用独立标识。 */
const char *gs232b_management_version(void);

#endif
