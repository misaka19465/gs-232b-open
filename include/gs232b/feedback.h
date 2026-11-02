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

#ifndef GS232B_FEEDBACK_H
#define GS232B_FEEDBACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gs232b/coordinates.h"
#include "gs232b/ports.h"

#define GS232B_FEEDBACK_FILTER_WINDOW 5U
#define GS232B_FEEDBACK_SAMPLE_MAX_AGE_US UINT64_C(100000)
#define GS232B_FEEDBACK_ZERO_RECORD_SIZE 26U /* NOTE: 记录头、4 字节 payload 和 CRC。 */
#define GS232B_FEEDBACK_ZERO_SCHEMA_VERSION 1U

typedef struct {
    uint32_t values_uv[GS232B_FEEDBACK_FILTER_WINDOW];
    size_t count;
    size_t next;
    gs232b_time_us_t latest_at_us;
} gs232b_feedback_filter_t;

typedef struct {
    gs232b_time_us_t acquired_at_us;
    uint32_t adc_uv;
    bool valid;
} gs232b_feedback_reading_t;

/**
 * @brief 清空固定容量的反馈滤波器。
 * @param filter 待初始化的滤波器。
 */
void gs232b_feedback_filter_init(gs232b_feedback_filter_t *filter);

/**
 * @brief 校验芯片校准后的 ADC 采样是否有效且足够新。
 * @param sample ADC 驱动提供的采样。
 * @param now_us 当前单调时间。
 * @return 采样无效、来自未来或超过 100ms 时返回 GS232B_ERROR_NOT_READY；
 *         校准电压超过固定满量程时返回 GS232B_ERROR_HARDWARE_FAULT。
 */
gs232b_error_t gs232b_feedback_sample_validate(const gs232b_sample_t *sample,
                                               gs232b_time_us_t now_us);

/**
 * @brief 将一个新鲜的校准电压加入有界去尖峰滤波器。
 * @param filter 每轴独立拥有的滤波器。
 * @param sample ADC 驱动提供的校准电压样本；raw 不参与电压换算。
 * @param now_us 当前单调时间。
 * @return 非法参数、陈旧采样或反馈越界时返回对应错误。
 */
gs232b_error_t gs232b_feedback_filter_push(gs232b_feedback_filter_t *filter,
                                            const gs232b_sample_t *sample,
                                            gs232b_time_us_t now_us);

/**
 * @brief 读取新鲜滤波值；排序后去掉两端各一个值并平均剩余值。
 * @param filter 每轴独立拥有的滤波器。
 * @param now_us 当前单调时间。
 * @param reading 输出值；错误时 valid 为 false。
 * @return 滤波结果不存在或已陈旧时返回 GS232B_ERROR_NOT_READY。
 */
gs232b_error_t gs232b_feedback_filter_read(const gs232b_feedback_filter_t *filter,
                                            gs232b_time_us_t now_us,
                                            gs232b_feedback_reading_t *reading);

/**
 * @brief 校验候选零点的新鲜度及其到固定满量程的最小跨度。
 * @param axis 零点所属轴。
 * @param reading 已滤波反馈读数。
 * @param now_us 当前单调时间。
 * @return 越界或有效跨度不足时返回 GS232B_ERROR_HARDWARE_FAULT。
 */
gs232b_error_t gs232b_feedback_zero_validate(gs232b_axis_id_t axis,
                                              const gs232b_feedback_reading_t *reading,
                                              gs232b_time_us_t now_us);

/**
 * @brief 校验后只写入指定轴的独立零点存储域。
 * @param store 零点持久化端口。
 * @param axis 零点所属轴。
 * @param reading 已滤波反馈读数。
 * @param now_us 当前单调时间。
 * @return 校验或存储失败时返回对应错误，旧记录由存储端口保留。
 */
gs232b_error_t gs232b_feedback_zero_save(const gs232b_store_t *store,
                                          gs232b_axis_id_t axis,
                                          const gs232b_feedback_reading_t *reading,
                                          gs232b_time_us_t now_us);

/**
 * @brief 从指定轴的独立存储域加载并验证零点记录。
 * @param store 零点持久化端口。
 * @param axis 零点所属轴。
 * @param zero_uv 输出零点电压；错误时置为零。
 * @return 无记录时返回 GS232B_ERROR_NOT_READY，记录损坏时返回硬件故障。
 */
gs232b_error_t gs232b_feedback_zero_load(const gs232b_store_t *store,
                                          gs232b_axis_id_t axis,
                                          uint32_t *zero_uv);

#endif
