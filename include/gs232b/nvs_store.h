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

#ifndef GS232B_NVS_STORE_H
#define GS232B_NVS_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "gs232b/ports.h"

#ifdef ESP_PLATFORM
#include "nvs.h"
#else
typedef uint32_t nvs_handle_t;
#endif

typedef struct {
    nvs_handle_t handle;
    bool open;
    char namespace_name[16];
    uint8_t active_slot[GS232B_STORE_DOMAIN_COUNT];
    bool active_slot_valid[GS232B_STORE_DOMAIN_COUNT];
} gs232b_nvs_store_t;

/**
 * @brief 打开用于分域双槽记录的 NVS namespace。
 * @param store 适配器实例。
 * @param name NVS namespace，遵守 ESP-IDF 的长度限制。
 * @return 打开失败时返回 GS232B_ERROR_IO。
 */
gs232b_error_t gs232b_nvs_store_open(gs232b_nvs_store_t *store, const char *name);

/**
 * @brief 关闭 NVS handle；不会擦除任何配置。
 */
void gs232b_nvs_store_close(gs232b_nvs_store_t *store);

/**
 * @brief 取得可交给配置/校准层的分域存储端口。
 */
gs232b_store_t gs232b_nvs_store_port(gs232b_nvs_store_t *store);

#endif
