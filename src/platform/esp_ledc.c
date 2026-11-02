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

#include "gs232b/esp_ledc.h"

#include <stddef.h>

#include "driver/ledc.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#if defined(CONFIG_ESP_CONSOLE_UART) && CONFIG_ESP_CONSOLE_UART_NUM == 0
#error "GPIO20 is reserved for SPEED_PWM; disable the UART0 console"
#endif

/* NOTE: 仅最大位宽不能写 2^N；固定 10bit 小于 C3 的 14bit，1024 是合法满幅。
 * 修改位宽时必须重新核对目标约束，不能静默把满幅减成 2^N-1。 */
_Static_assert(GS232B_LEDC_RESOLUTION_BITS < SOC_LEDC_TIMER_BIT_WIDTH,
               "Full duty requires resolution below the hardware maximum");
_Static_assert(GS232B_LEDC_RESOLUTION_BITS == LEDC_TIMER_10_BIT,
               "LEDC timer and duty conversion must use the same resolution");

static gs232b_error_t map_error(esp_err_t error)
{
    switch (error) {
    case ESP_OK: return GS232B_OK;
    case ESP_ERR_INVALID_ARG: return GS232B_ERROR_INVALID_ARGUMENT;
    case ESP_ERR_INVALID_STATE: return GS232B_ERROR_NOT_READY;
    case ESP_ERR_NO_MEM: return GS232B_ERROR_RESOURCE_EXHAUSTED;
    case ESP_ERR_TIMEOUT: return GS232B_ERROR_TIMEOUT;
    default: return GS232B_ERROR_IO;
    }
}

gs232b_error_t gs232b_esp_ledc_duty_from_permille(uint16_t permille, uint32_t *duty)
{
    if (duty == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *duty = 0;
    if (permille > 1000U) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *duty = ((uint32_t)permille * GS232B_LEDC_FULL_DUTY + 500U) / 1000U;
    return GS232B_OK;
}

gs232b_error_t gs232b_esp_ledc_set_duty(gs232b_esp_ledc_t *adapter, uint32_t duty)
{
    if (adapter == NULL || duty > GS232B_LEDC_FULL_DUTY) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!adapter->opened) {
        return GS232B_ERROR_NOT_READY;
    }
    esp_err_t error = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (error == ESP_OK) {
        error = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    return map_error(error);
}

gs232b_error_t gs232b_esp_ledc_set_permille(gs232b_esp_ledc_t *adapter,
                                          uint16_t permille)
{
    uint32_t duty;
    const gs232b_error_t error = gs232b_esp_ledc_duty_from_permille(permille, &duty);
    return error != GS232B_OK ? error : gs232b_esp_ledc_set_duty(adapter, duty);
}

gs232b_error_t gs232b_esp_ledc_close(gs232b_esp_ledc_t *adapter)
{
    if (adapter == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    adapter->opened = false;
    if (adapter->channel_open) {
        esp_err_t error = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        if (error != ESP_OK) { return map_error(error); }
        const ledc_channel_config_t channel = {
            .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
            .deconfigure = true
        };
        error = ledc_channel_config(&channel);
        if (error != ESP_OK) { return map_error(error); }
        adapter->channel_open = false;
    }
    if (adapter->timer_open) {
        esp_err_t error = ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
        if (error != ESP_OK) { return map_error(error); }
        const ledc_timer_config_t timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
            .deconfigure = true
        };
        error = ledc_timer_config(&timer);
        if (error != ESP_OK) { return map_error(error); }
        adapter->timer_open = false;
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_esp_ledc_open(gs232b_esp_ledc_t *adapter)
{
    if (adapter == NULL || adapter->timer_open || adapter->channel_open ||
        adapter->opened) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = GS232B_LEDC_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t error = ledc_timer_config(&timer);
    if (error != ESP_OK) { return map_error(error); }
    adapter->timer_open = true;
    const ledc_channel_config_t channel = {
        .gpio_num = GS232B_BOARD_SPEED_PWM_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0, .hpoint = 0
    };
    /* NOTE: 配置失败也可能已接通 GPIO；保留资源标志以便 close 拉低并释放。 */
    adapter->channel_open = true;
    error = ledc_channel_config(&channel);
    if (error != ESP_OK) {
        (void)gs232b_esp_ledc_close(adapter);
        return map_error(error);
    }
    adapter->opened = true;
    return GS232B_OK;
}
