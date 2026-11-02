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

#ifndef GS232B_APP_H
#define GS232B_APP_H

#include "gs232b/gs_dispatcher.h"
#include "gs232b/w5500_reset.h"

#define GS232B_APP_CONTROL_PERIOD_US UINT64_C(20000)
#define GS232B_APP_SAMPLE_PERIOD_US UINT64_C(5000)
#define GS232B_APP_REVERSAL_US UINT64_C(250000)

typedef enum {
    GS232B_APP_COLD = 0,
    GS232B_APP_RESET_WAIT,
    GS232B_APP_UART_BOOT,
    GS232B_APP_RUNNING,
    GS232B_APP_FAILED
} gs232b_app_phase_t;

typedef struct {
    gs232b_app_phase_t phase;
    gs232b_error_t fatal_error, store_error, adc_error, uart_error, network_error;
    gs232b_error_t zero_error[GS232B_AXIS_COUNT];
    bool uart_ready;
} gs232b_app_status_t;

typedef struct gs232b_app gs232b_app_t;

/* NOTE: 生命周期入口只在启动时执行；service 必须有界、不得等待网络就绪。
 * 所有端口借用 ctx 至设备停机，失败也须提供有效的降级 store/sampler。
 * store 成功必须表示已落盘；平台在独立任务执行写入，并拒绝非全停写入。 */
typedef struct {
    void *ctx;
    gs232b_clock_t clock;
    gs232b_session_io_t io;
    gs232b_error_t (*outputs_open)(void *ctx, gs232b_shared_spi_t **bus);
    gs232b_error_t (*store_open)(void *ctx, gs232b_store_t *store);
    gs232b_error_t (*adc_open)(void *ctx, gs232b_sampler_t *sampler);
    gs232b_error_t (*uart_open)(void *ctx, gs232b_store_t store);
    gs232b_error_t (*uart_boot)(void *ctx);
    gs232b_error_t (*uart_attach)(void *ctx, gs232b_dispatcher_t *dispatcher);
    gs232b_error_t (*uart_service)(void *ctx);
    gs232b_error_t (*network_bind)(void *ctx, gs232b_app_t *app);
    gs232b_error_t (*network_start)(void *ctx, gs232b_app_t *app);
    gs232b_error_t (*network_service)(void *ctx, gs232b_app_t *app);
} gs232b_app_platform_t;

/* NOTE: 单一控制任务独占全部核心和 reset；不得复制活动实例或从 SDK 回调重入。
 * 请求池、序列双缓冲和会话槽固定有界；平台 RX 只能投递带会话/epoch 的事件。 */
struct gs232b_app {
    gs232b_app_platform_t platform;
    gs232b_app_status_t status;
    gs232b_shared_spi_t *bus;
    gs232b_output_manager_t outputs;
    gs232b_w5500_reset_t startup_reset;
    gs232b_motion_config_t config;
    gs232b_motion_t motion;
    gs232b_dispatcher_t dispatcher;
    gs232b_store_t store;
    uint8_t requests[GS232B_LINE_STORAGE_BYTES];
};

/**
 * @brief 首先锁存 0x00/PWM=0，再非阻塞等待硬复位，所有机型统一等待换向 250ms。
 * @note 后续 step 在存储就绪后加载型号配置及独立轴零点，再初始化运动核心。
 */
gs232b_error_t gs232b_app_start(gs232b_app_t *app, gs232b_app_platform_t platform);
/** @brief 串行推进启动或一轮运行；网络/ADC 失败降级，UART 不等待网络。 */
gs232b_error_t gs232b_app_step(gs232b_app_t *app);
/** @brief 复制启动诊断；缺零点、存储、ADC 和网络失败分别保留，不伪装就绪。 */
gs232b_app_status_t gs232b_app_status(const gs232b_app_t *app);

#endif
