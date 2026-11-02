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

#include "rmt_api.h"
#include "driver/gpio.h"
#include <assert.h>
#include <string.h>

struct fake_rmt_channel { unsigned unused; };
static struct fake_rmt_channel channel;
fake_rmt_t fake_rmt;

void fake_rmt_reset(void)
{
    assert(!fake_rmt.allocated);
    memset(&fake_rmt, 0, sizeof(fake_rmt));
    fake_rmt.rx_high = true;
}

int gpio_get_level(int pin)
{
    assert(pin == 7 && fake_rmt.allocated);
    return fake_rmt.rx_high;
}

static esp_err_t init_result(void)
{
    return ++fake_rmt.init_calls == fake_rmt.fail_init_at ? ESP_FAIL : ESP_OK;
}

esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *config,
                             rmt_channel_handle_t *result)
{
    assert(!fake_rmt.allocated);
    esp_err_t error = init_result();
    if (error != ESP_OK) { return error; }
    fake_rmt.allocated = true;
    fake_rmt.config = *config;
    *result = &channel;
    return ESP_OK;
}

esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t handle,
    const rmt_rx_event_callbacks_t *callbacks, void *ctx)
{
    assert(handle == &channel && fake_rmt.allocated && !fake_rmt.enabled);
    esp_err_t error = init_result();
    if (error == ESP_OK) { fake_rmt.callback = callbacks->on_recv_done; fake_rmt.ctx = ctx; }
    return error;
}

esp_err_t rmt_enable(rmt_channel_handle_t handle)
{
    assert(handle == &channel && fake_rmt.allocated && !fake_rmt.enabled);
    esp_err_t error = init_result();
    fake_rmt.enabled = error == ESP_OK;
    return error;
}

esp_err_t rmt_receive(rmt_channel_handle_t handle, void *buffer, size_t size,
                       const rmt_receive_config_t *config)
{
    assert(handle == &channel && fake_rmt.enabled && !fake_rmt.receiving);
    ++fake_rmt.receives;
    const esp_err_t error = init_result();
    if (error != ESP_OK || fake_rmt.fail_receive) { return ESP_FAIL; }
    fake_rmt.receiving = true;
    fake_rmt.buffer = buffer;
    fake_rmt.buffer_size = size;
    fake_rmt.receive_config = *config;
    return ESP_OK;
}

void fake_rmt_complete(const rmt_symbol_word_t *symbols, size_t count, bool last)
{
    assert(fake_rmt.receiving && fake_rmt.callback != NULL);
    size_t size = count * sizeof(*symbols);
    if (size > fake_rmt.buffer_size) { size = fake_rmt.buffer_size; }
    if (size != 0) { memcpy(fake_rmt.buffer, symbols, size); }
    if (last) { fake_rmt.receiving = false; }
    ++fake_rmt.completions;
    const rmt_rx_done_event_data_t event = {
        .received_symbols = fake_rmt.buffer + (fake_rmt.wrong_buffer ? 1 : 0),
        .num_symbols = count, .flags.is_last = last
    };
    (void)fake_rmt.callback(&channel, &event, fake_rmt.ctx);
}

esp_err_t rmt_disable(rmt_channel_handle_t handle)
{
    assert(handle == &channel && fake_rmt.enabled);
    if (fake_rmt.fail_disable) { return ESP_FAIL; }
    fake_rmt.enabled = fake_rmt.receiving = false;
    return ESP_OK;
}

esp_err_t rmt_del_channel(rmt_channel_handle_t handle)
{
    assert(handle == &channel && fake_rmt.allocated && !fake_rmt.enabled);
    if (fake_rmt.fail_delete) { return ESP_FAIL; }
    fake_rmt.allocated = false;
    return ESP_OK;
}
