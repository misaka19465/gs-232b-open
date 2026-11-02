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

#ifndef GS232B_ESP_STORE_WORKER_H
#define GS232B_ESP_STORE_WORKER_H

#include "gs232b/motion.h"
#include "gs232b/nvs_store.h"
#include "gs232b/persistence.h"
#include "gs232b/command_control.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef struct {
    uint8_t bytes[GS232B_PERSISTENCE_MAX_RECORD_SIZE];
    size_t size;
    gs232b_error_t error;
} gs232b_store_cache_t;

/* NOTE: 控制任务独占 cache，只有请求/完成队列跨任务传递；NVS handle 由 worker 独占。
 * 一次只允许一个同步持久化事务，全停后才可等待，期间不执行运动或接纳新命令。 */
typedef struct {
    gs232b_nvs_store_t nvs;
    gs232b_store_cache_t current[GS232B_STORE_DOMAIN_COUNT];
    gs232b_store_cache_t previous[GS232B_STORE_DOMAIN_COUNT];
    QueueHandle_t requests, results;
    TaskHandle_t task;
    gs232b_motion_t *motion;
    gs232b_output_manager_t *outputs;
    const gs232b_control_t *control;
    gs232b_error_t open_error;
} gs232b_esp_store_worker_t;

/** @brief 启动独立 NVS owner 并预读分域双槽；不擦除损坏记录或整个 NVS 分区。 */
gs232b_error_t gs232b_esp_store_worker_open(gs232b_esp_store_worker_t *worker,
    gs232b_motion_t *motion, gs232b_output_manager_t *outputs, const gs232b_control_t *control);
/** @brief 借用 RAM 读端口和仅全停可用的持久化端口；启动失败以无记录降级，写入保留原错。 */
gs232b_store_t gs232b_esp_store_worker_port(gs232b_esp_store_worker_t *worker);
/** @brief 仅在已知全停且所有借用结束后关闭 NVS owner，等待确认退出再释放队列。 */
gs232b_error_t gs232b_esp_store_worker_close(gs232b_esp_store_worker_t *worker);
#endif
#endif
