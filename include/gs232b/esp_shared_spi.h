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

#ifndef GS232B_ESP_SHARED_SPI_H
#define GS232B_ESP_SHARED_SPI_H

#include "gs232b/shared_spi.h"
#include "gs232b/esp_ledc.h"

#ifdef ESP_PLATFORM
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define GS232B_BOARD_SPI_CLOCK_HZ 1000000
#define GS232B_BOARD_SPI_TIMEOUT_MS 50U

/* NOTE: 私有布局仅用于静态分配；仅本模块可访问两个原始 device handle。
 * 一个 SPI2_HOST 仅建立一个实例，不接受外部注册、排队或 bit-bang 访问。 */
typedef struct {
    gs232b_shared_spi_t bus;
    spi_device_handle_t shift;
    spi_device_handle_t ethernet;
    SemaphoreHandle_t mutex;
    TickType_t timeout_ticks;
    bool host_open;
    gs232b_esp_ledc_t pwm;
} gs232b_esp_shared_spi_t;

/**
 * @brief 初始化板上 SPI2_HOST、GPIO2 锁存、GPIO20 LEDC 和两个 mode0 device。
 * @param adapter 清零且未活动的实例，失败后释放已取得资源，可再次 open。
 * @param clock_hz 两个器件共用频率，范围 1..GS232B_BOARD_SPI_CLOCK_HZ。
 * @param timeout_ms 入口互斥锁有限等待，不包括物理传输时间；必须非零。
 * @note 1MHz 是待板验证的保守起点，不代表已验证的 595 电气时序上限。
 *       该函数将 STCP 置低但不锁存；随后必须尽早初始化输出管理器提交 0x00。
 */
gs232b_error_t gs232b_esp_shared_spi_open(gs232b_esp_shared_spi_t *adapter,
                                          int clock_hz, uint32_t timeout_ms);

/**
 * @brief 释放外设；调用方先停止网络任务、销毁 MAC/PHY 并成功提交全停。
 * @note 不替代停机，也不产生锁存脉冲；须在所有并发调用结束后执行。
 */
gs232b_error_t gs232b_esp_shared_spi_close(gs232b_esp_shared_spi_t *adapter);

#endif
#endif
