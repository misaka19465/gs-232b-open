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

#include "esp_attr.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "soc/soc_caps.h"

_Static_assert(SOC_RMT_SUPPORTED && SOC_RMT_SUPPORT_RX_PINGPONG,
               "UART RX capture requires RMT RX ping-pong support");
_Static_assert(SOC_RMT_MEM_WORDS_PER_CHANNEL == 48,
               "Revalidate the capture budget for a different target");

typedef struct {
    size_t count;
    int64_t completed_at_us;
    uint32_t sequence;
    bool invalid;
    bool last;
    bool idle_high;
    rmt_symbol_word_t symbols[GS232B_UART_CAPTURE_SYMBOLS];
} capture_event_t;

/* NOTE: 1MHz 提供 1us 位宽；12ms idle 大于 1200bps CR 最长的 4bit 低电平，
 * 且低于 C3 的 32767 tick 上限。1us 毛刺过滤为 APB 80MHz 的 80 tick。 */
static const rmt_receive_config_t receive_config = {
    .signal_range_min_ns = 1000,
    .signal_range_max_ns = 12000000,
    .flags.en_partial_rx = true
};

static bool IRAM_ATTR received(rmt_channel_handle_t channel,
    const rmt_rx_done_event_data_t *event, void *ctx)
{
    gs232b_uart_capture_t *capture = ctx;
    capture_event_t completed = {
        .count = event->num_symbols, .completed_at_us = esp_timer_get_time(),
        .sequence = ++capture->published, .last = event->flags.is_last,
        .idle_high = event->flags.is_last && gpio_get_level(7) != 0,
        .invalid = channel != capture->channel ||
            event->received_symbols != capture->symbols ||
            event->num_symbols > GS232B_UART_CAPTURE_SYMBOLS
    };
    if (!completed.invalid) {
        for (size_t i = 0; i < completed.count; ++i) {
            completed.symbols[i] = event->received_symbols[i];
        }
    }
    /* NOTE: C3 无锁原子 bool 不成立；使用 RTOS 的 ISR 队列发布所有权。
     * 覆盖尚未消费的旧块时序号出现缺口，控制任务清除候选，保留最新 last
     * 事件使通道仍能重启。符号按值复制，绝不排队 SDK 即将复用的指针。 */
    BaseType_t woken = pdFALSE;
    (void)xQueueOverwriteFromISR(capture->completions, &completed, &woken);
    return woken == pdTRUE;
}

gs232b_error_t gs232b_uart_capture_open(gs232b_uart_capture_t *capture)
{
    gs232b_uart_baud_reset(&capture->detector);
    capture->enabled = capture->receiving = false;
    capture->published = capture->consumed = 0;
    capture->completions = xQueueCreate(1, sizeof(capture_event_t));
    if (capture->completions == NULL) { return GS232B_ERROR_RESOURCE_EXHAUSTED; }
    const rmt_rx_channel_config_t config = {
        .gpio_num = 7, .clk_src = RMT_CLK_SRC_APB, .resolution_hz = 1000000,
        .mem_block_symbols = 48
    };
    esp_err_t error = rmt_new_rx_channel(&config, &capture->channel);
    const rmt_rx_event_callbacks_t callbacks = {.on_recv_done = received};
    if (error == ESP_OK) {
        error = rmt_rx_register_event_callbacks(capture->channel, &callbacks, capture);
    }
    if (error == ESP_OK) {
        error = rmt_enable(capture->channel);
        capture->enabled = error == ESP_OK;
    }
    if (error == ESP_OK) {
        error = rmt_receive(capture->channel, capture->symbols,
                            sizeof(capture->symbols), &receive_config);
        capture->receiving = error == ESP_OK;
    }
    return gs232b_uart_error(error);
}

static void decode(gs232b_uart_capture_t *capture, const capture_event_t *event)
{
    if (event->invalid) { gs232b_uart_baud_reset(&capture->detector); return; }
    bool end = false;
    bool invalid = false;
    /* NOTE: partial 块不含 EOF；末块 EOF 后出现非零区间不可信，先整体校验
     * 再交给判定器，防止已锁存的第三个 CR 掩盖捕获缺口。 */
    for (size_t i = 0; i < event->count; ++i) {
        const rmt_symbol_word_t symbol = event->symbols[i];
        if (symbol.duration0 == 0) { end = true; }
        else if (end) { invalid = true; }
        if (symbol.duration1 == 0) { end = true; }
        else if (end) { invalid = true; }
    }
    if (invalid || (end && !event->last)) {
        gs232b_uart_baud_reset(&capture->detector);
        return;
    }
    for (size_t i = 0; i < event->count; ++i) {
        const rmt_symbol_word_t symbol = event->symbols[i];
        if (symbol.duration0 == 0) { break; }
        gs232b_uart_baud_pulse(&capture->detector, symbol.level0, symbol.duration0);
        if (symbol.duration1 == 0) { break; }
        gs232b_uart_baud_pulse(&capture->detector, symbol.level1, symbol.duration1);
    }
    /* NOTE: RMT 可能把导致 RX_DONE 的最终 idle 区间写成零时长 EOF。
     * 完整 CR 数据尾部 L4 已验证，且 ISR 确认 GPIO7 仍为高时，硬件 12ms
     * idle 阈值已覆盖停止位；不能用 EOF 自行补齐缺少的数据位或低电平尾部。 */
    if (event->last && end && event->idle_high && capture->detector.phase == 5) {
        gs232b_uart_baud_pulse(&capture->detector, true, 12000);
    }
    if (event->last) { gs232b_uart_baud_end_capture(&capture->detector); }
}

gs232b_error_t gs232b_uart_capture_service(gs232b_uart_capture_t *capture,
                                          int64_t deadline_us)
{
    capture_event_t completed;
    if (xQueueReceive(capture->completions, &completed, 0) == pdPASS) {
        if (completed.last) { capture->receiving = false; }
        if (completed.sequence != capture->consumed + 1U) {
            gs232b_uart_baud_reset(&capture->detector);
        }
        capture->consumed = completed.sequence;
        if (completed.completed_at_us < deadline_us) { decode(capture, &completed); }
    }
    if (!capture->receiving && capture->detector.baud == 0 &&
        esp_timer_get_time() < deadline_us) {
        const esp_err_t error = rmt_receive(capture->channel, capture->symbols,
            sizeof(capture->symbols), &receive_config);
        capture->receiving = error == ESP_OK;
        return gs232b_uart_error(error);
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_uart_capture_close(gs232b_uart_capture_t *capture)
{
    if (capture->channel != NULL && capture->enabled) {
        const esp_err_t error = rmt_disable(capture->channel);
        if (error != ESP_OK) { return gs232b_uart_error(error); }
        capture->enabled = capture->receiving = false;
    }
    if (capture->channel != NULL) {
        const esp_err_t error = rmt_del_channel(capture->channel);
        if (error != ESP_OK) { return gs232b_uart_error(error); }
        capture->channel = NULL;
    }
    if (capture->completions != NULL) {
        vQueueDelete(capture->completions);
        capture->completions = NULL;
    }
    return GS232B_OK;
}
