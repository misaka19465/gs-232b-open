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

#ifndef GS232B_ESP_UART_INTERNAL_H
#define GS232B_ESP_UART_INTERNAL_H

#include "gs232b/esp_uart.h"
#include "driver/uart.h"
#include "sdkconfig.h"

#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || !CONFIG_ESP_CONSOLE_SECONDARY_NONE || \
    (defined(CONFIG_ESP_CONSOLE_UART) && CONFIG_ESP_CONSOLE_UART)
#error "Protocol UART requires the native USB Serial/JTAG console only"
#endif

/** @brief 映射 IDF 错误，保留资源/参数诊断。 */
gs232b_error_t gs232b_uart_error(esp_err_t error);
/** @brief 为 GPIO7 建立真实 RMT RX 捕获；失败资源由 close 重试释放。 */
gs232b_error_t gs232b_uart_capture_open(gs232b_uart_capture_t *capture);
/** @brief 消费一次已完成的捕获；超过截止时间的结果不得参与识别。 */
gs232b_error_t gs232b_uart_capture_service(gs232b_uart_capture_t *capture,
                                          int64_t deadline_us);
/** @brief 停用并删除 RMT 通道；失败时保留句柄供控制任务重试。 */
gs232b_error_t gs232b_uart_capture_close(gs232b_uart_capture_t *capture);
/** @brief 持 RX 锁时合并接收故障并丢弃不再可信的数据；清理失败持续保留故障。 */
void gs232b_uart_loss_locked(gs232b_esp_uart_t *adapter);
/** @brief 持 RX 锁时检查完整有界事件队列，先故障后数据。 */
void gs232b_uart_events_locked(gs232b_esp_uart_t *adapter);
/** @brief 专用 RX 任务的一次有界搬运；测试替身可确定性调用，不能与该任务并发。 */
void gs232b_uart_rx_step(gs232b_esp_uart_t *adapter);
/** @brief 专用 RX 任务入口，只接收字节，不解析协议或驱动运动。 */
void gs232b_uart_rx_task(void *ctx);

#endif
