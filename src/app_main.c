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

#include "app/esp_app_internal.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static gs232b_esp_app_t runtime;

static void control(void *ctx)
{
    gs232b_esp_app_t *app = ctx;
    TickType_t deadline = xTaskGetTickCount();
    int64_t diagnostic_at = 0;
    while (app->app.status.phase != GS232B_APP_FAILED) {
        (void)gs232b_app_step(&app->app);
        if (esp_timer_get_time() - diagnostic_at >= 5000000) {
            diagnostic_at = esp_timer_get_time();
            ESP_LOGI("gs_diag", "phase=%u eth=%u wifi=%u sdk=%ld heap=%u largest=%u",
                (unsigned)app->app.status.phase, (unsigned)app->eth.state.last_error,
                (unsigned)app->wifi.state.last_error, (long)app->wifi.state.sdk_error,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        }
        if (app->app.status.phase == GS232B_APP_UART_BOOT) {
            /* NOTE: 匹配窗口内尚未开放运动，1 tick 轮询避免积压单槽 RMT 完成事件。 */
            vTaskDelay(1);
            deadline = xTaskGetTickCount();
            continue;
        }
        /* NOTE: 绝对周期避免执行耗时累计漂移；过期不补跑历史运动/采样。 */
        if (xTaskDelayUntil(&deadline, pdMS_TO_TICKS(20)) != pdTRUE) deadline = xTaskGetTickCount();
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* NOTE: 首个任务先执行安全锁存；任何 NVS、网络及采集初始化均在其后。 */
    const gs232b_error_t error = gs232b_app_start(&runtime.app,
        gs232b_esp_app_platform(&runtime));
    if (error != GS232B_OK) {
        return;
    }
    if (xTaskCreate(control, "gs_control", 10240, &runtime, 10, NULL) != pdPASS) {
        runtime.app.status.phase = GS232B_APP_FAILED;
        runtime.app.status.fatal_error = GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
}
