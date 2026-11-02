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

#ifndef GS232B_W5500_RESET_H
#define GS232B_W5500_RESET_H

#include "gs232b/shared_spi.h"

#define GS232B_W5500_RESET_MIN_US UINT64_C(500)
#define GS232B_W5500_RECOVERY_MIN_US UINT64_C(1000)
#define GS232B_W5500_RESET_DEFAULT_US UINT64_C(1000)
#define GS232B_W5500_RECOVERY_DEFAULT_US UINT64_C(10000)

typedef enum {
    GS232B_W5500_OFFLINE = 0,
    GS232B_W5500_QUIESCING,
    GS232B_W5500_RESET_LOW,
    GS232B_W5500_RECOVERING,
    GS232B_W5500_RESUMING,
    GS232B_W5500_READY,
    GS232B_W5500_FAILED
} gs232b_w5500_reset_phase_t;

/* NOTE: 回调必须有界、不得重入协调器。WOULD_BLOCK 表示网络任务尚在处理，
 * 后续 tick 会重复调用；OK 表示该阶段完成，其他错误终止恢复。
 * quiesce 停止网络提交/收包并停用旧驱动，首次启动也调用；门在回调成功后关闭。
 * resume 在门打开后重新探测/初始化 MAC/PHY 并恢复网络，不能只 esp_eth_start
 * 已被硬复位而丢失配置的设备。运行时先由网络层处理会话失效/所有者停止事件。 */
typedef struct {
    void *ctx;
    gs232b_error_t (*quiesce)(void *ctx);
    gs232b_error_t (*resume)(void *ctx);
} gs232b_w5500_lifecycle_t;

/* NOTE: 与输出管理器在同一串行所有权边界内调用全部 API；等待期间仍推进运动
 * 和停止事件。不得从网络回调直接调用管理器。失败不自动重试或改写 bit4。
 * 输出提交错误后，调用方须先推进运动故障处理，再显式重试全停/网络恢复。 */
typedef struct {
    gs232b_output_manager_t *outputs;
    gs232b_shared_spi_t *bus;
    gs232b_w5500_lifecycle_t lifecycle;
    gs232b_time_us_t reset_us;
    gs232b_time_us_t recovery_us;
    gs232b_time_us_t phase_started_us;
    gs232b_w5500_reset_phase_t phase;
    gs232b_error_t last_error;
} gs232b_w5500_reset_t;

/**
 * @brief 配置外置复位协调器；不操作硬件。复位脉宽至少 500us，恢复至少 1ms。
 * @param outputs 已初始化、且使用 bus 输出驱动的唯一管理器。
 * @param bus 已初始化的共享总线；首次启动时门保持默认关闭。
 */
gs232b_error_t gs232b_w5500_reset_init(gs232b_w5500_reset_t *reset,
                                       gs232b_output_manager_t *outputs,
                                       gs232b_shared_spi_t *bus,
                                       gs232b_w5500_lifecycle_t lifecycle,
                                       gs232b_time_us_t reset_us,
                                       gs232b_time_us_t recovery_us);

/** @brief 请求首次启动或运行中恢复；进行中返回 BUSY，失败须显式重新请求。 */
gs232b_error_t gs232b_w5500_reset_begin(gs232b_w5500_reset_t *reset);

/**
 * @brief 非阻塞推进停用、排空、bit4 复位、恢复等待及网络重建。
 * @return OK 可表示等待中，检查 phase==READY；失败保留 last_error 并关闭访问门。
 * @note 计时从各次成功输出提交完成后开始，网络回调不在 SPI 锁内执行。
 *       恢复期间输出变为未知则终止，不以新的 reset 提交掩盖运动故障。
 *       bit4 由协调器独占，其他完整请求必须保留其当前逻辑值。
 */
gs232b_error_t gs232b_w5500_reset_tick(gs232b_w5500_reset_t *reset);

#endif
