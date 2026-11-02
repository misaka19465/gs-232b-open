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

#include "esp_uart_internal.h"

void gs232b_uart_loss_locked(gs232b_esp_uart_t *adapter)
{
    if (!adapter->loss_pending) { adapter->loss_reported = false; }
    adapter->loss_pending = true;
    (void)xQueueReset(adapter->packets);
    /* NOTE: 不能越过未知缺口继续解析；清理失败由 service 再试且不交付数据。 */
    (void)uart_flush_input(UART_NUM_1);
}

void gs232b_uart_events_locked(gs232b_esp_uart_t *adapter)
{
    /* NOTE: IDF 事件队列满时可能丢掉后续错误通知，按不可信输入保守停机。 */
    bool lost = uxQueueMessagesWaiting(adapter->events) >= GS232B_UART_EVENTS;
    uart_event_t event;
    size_t drained = 0;
    for (; drained < GS232B_UART_EVENTS; ++drained) {
        if (xQueueReceive(adapter->events, &event, 0) != pdPASS) { break; }
        switch (event.type) {
        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
        case UART_FRAME_ERR:
        case UART_PARITY_ERR:
        case UART_BREAK:
            lost = true;
            break;
        default:
            break;
        }
    }
    /* NOTE: ISR 也可能在初次长度检查后填满队列；完整耗尽处理预算同样按饱和处理。 */
    lost |= drained == GS232B_UART_EVENTS;
    if (lost) { gs232b_uart_loss_locked(adapter); }
}

void gs232b_uart_rx_step(gs232b_esp_uart_t *adapter)
{
    if (!atomic_load(&adapter->enabled)) { return; }
    if (xSemaphoreTake(adapter->rx_lock, portMAX_DELAY) != pdPASS) { return; }
    if (atomic_load(&adapter->enabled)) {
        gs232b_uart_events_locked(adapter);
        /* NOTE: 软件队列暂满时留在 32KiB driver ring；不主动读出再丢弃。 */
        if (!adapter->loss_pending && uxQueueSpacesAvailable(adapter->packets) != 0) {
            gs232b_uart_packet_t packet = {.context = adapter->rx_context};
            const int count = uart_read_bytes(UART_NUM_1, packet.bytes,
                                               sizeof(packet.bytes), 0);
            if (count < 0 || (size_t)count > sizeof(packet.bytes)) {
                gs232b_uart_loss_locked(adapter);
            } else if (count > 0) {
                packet.size = (size_t)count;
                if (xQueueSend(adapter->packets, &packet, 0) != pdPASS) {
                    gs232b_uart_loss_locked(adapter);
                }
            }
        }
    }
    (void)xSemaphoreGive(adapter->rx_lock);
}

void gs232b_uart_rx_task(void *ctx)
{
    gs232b_esp_uart_t *adapter = ctx;
    while (!atomic_load(&adapter->stopping)) {
        gs232b_uart_rx_step(adapter);
        /* NOTE: 每轮最多一个 256 字节块；持续让出 CPU，绝不等待协议消费。 */
        vTaskDelay(1);
    }
    (void)xSemaphoreGive(adapter->stopped);
    vTaskDelete(NULL);
}
