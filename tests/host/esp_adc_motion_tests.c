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

#include "esp_adc_test_support.h"
#include "gs232b/motion.h"

typedef struct {
    gs232b_esp_adc_t adc;
    gs232b_output_manager_t manager;
    gs232b_motion_t motion;
    uint8_t shadow;
    uint16_t pwm;
} fixture_t;

static gs232b_time_us_t now(void *ctx)
{
    (void)ctx;
    return (gs232b_time_us_t)esp_timer_get_time();
}

static gs232b_error_t apply(void *ctx, uint8_t shadow, uint16_t pwm)
{
    fixture_t *fixture = ctx;
    fixture->shadow = shadow;
    fixture->pwm = pwm;
    return GS232B_OK;
}

static bool init_motion(fixture_t *fixture)
{
    gs232b_motion_config_t config;
    gs232b_motion_config_init(&config);
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        config.axes[axis].enabled = true;
        config.axes[axis].zero_valid = true;
        config.axes[axis].zero_uv = 100000;
    }
    const gs232b_clock_t clock = {.now_us = now};
    const gs232b_output_driver_t driver = {.ctx = fixture, .apply = apply};
    CHECK(gs232b_output_manager_init(&fixture->manager, clock, driver, 10) == GS232B_OK);
    CHECK(gs232b_motion_init(&fixture->motion, &fixture->manager,
                             gs232b_esp_adc_sampler(&fixture->adc), &config) == GS232B_OK);
    return true;
}

static bool test_invalid_feedback_stops_both_axes(void)
{
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        for (int failure = 0; failure < 3; ++failure) {
            esp_adc_fake_reset();
            fixture_t fixture = {0};
            CHECK(gs232b_esp_adc_open(&fixture.adc) == GS232B_OK);
            CHECK(gs232b_esp_adc_capture(&fixture.adc) == GS232B_OK);
            CHECK(init_motion(&fixture));
            const int32_t targets[] = {300000, 160000};
            CHECK(gs232b_motion_goto(&fixture.motion, GS232B_MOTION_ALL_AXES,
                                      targets) == GS232B_OK);
            esp_adc_fake.now_us = 10;
            CHECK(gs232b_esp_adc_capture(&fixture.adc) == GS232B_OK);
            CHECK(gs232b_motion_tick(&fixture.motion) == GS232B_OK);
            CHECK((fixture.shadow & 0x0fU) == 0x05U && fixture.pwm > 0);
            const int channel = axis == GS232B_AXIS_AZIMUTH ? 1 : 0;
            if (failure == 0) {
                esp_adc_fake.read_error[channel] = ESP_ERR_TIMEOUT;
            } else if (failure == 1) {
                esp_adc_fake.convert_error[channel] = ESP_ERR_INVALID_STATE;
            } else {
                esp_adc_fake.voltage_mv[channel] = 1800;
            }
            esp_adc_fake.now_us = 5010;
            CHECK(gs232b_esp_adc_capture(&fixture.adc) ==
                  (failure == 2 ? GS232B_OK : GS232B_ERROR_NOT_READY));
            CHECK(gs232b_motion_tick(&fixture.motion) ==
                  (failure == 2 ? GS232B_ERROR_HARDWARE_FAULT : GS232B_ERROR_NOT_READY));
            gs232b_motion_snapshot_t state;
            CHECK(gs232b_motion_snapshot(&fixture.motion, &state) == GS232B_OK);
            CHECK(state.fault.code == GS232B_FAULT_SAMPLE);
            CHECK(state.fault.axis_mask == (1U << axis));
            CHECK(!state.axes[axis].position_valid);
            CHECK(fixture.motion.axes[axis].filter.count == 0);
            CHECK(state.axes[0].mode == GS232B_AXIS_FAULT);
            CHECK(state.axes[1].mode == GS232B_AXIS_FAULT);
            CHECK(!state.axes[0].target_valid && !state.axes[1].target_valid);
            CHECK(state.output.known);
            CHECK((fixture.shadow & 0x0fU) == 0 && fixture.pwm == 0);
            if (failure != 2) { CHECK(!state.axes[axis].sample.valid); }
            CHECK(gs232b_motion_clear_fault(&fixture.motion) != GS232B_OK);
            esp_adc_fake.read_error[channel] = ESP_OK;
            esp_adc_fake.convert_error[channel] = ESP_OK;
            esp_adc_fake.voltage_mv[channel] = 600;
            /* NOTE: 清除 API 错误不足以复用缓存，须先得到新的校准采样。 */
            CHECK(gs232b_motion_clear_fault(&fixture.motion) != GS232B_OK);
            ++esp_adc_fake.now_us;
            CHECK(gs232b_esp_adc_capture(&fixture.adc) == GS232B_OK);
            CHECK(gs232b_motion_clear_fault(&fixture.motion) == GS232B_OK);
            CHECK(gs232b_motion_snapshot(&fixture.motion, &state) == GS232B_OK);
            CHECK(state.axes[0].mode == GS232B_AXIS_IDLE);
            CHECK(state.axes[1].mode == GS232B_AXIS_IDLE);
            CHECK((fixture.shadow & 0x0fU) == 0 && fixture.pwm == 0);
            CHECK(gs232b_esp_adc_close(&fixture.adc) == GS232B_OK);
        }
    }
    return true;
}

static bool test_stopped_producer_expires_and_stops_motion(void)
{
    esp_adc_fake_reset();
    fixture_t fixture = {0};
    CHECK(gs232b_esp_adc_open(&fixture.adc) == GS232B_OK);
    CHECK(gs232b_esp_adc_capture(&fixture.adc) == GS232B_OK);
    CHECK(init_motion(&fixture));
    const int32_t targets[] = {300000, 160000};
    CHECK(gs232b_motion_goto(&fixture.motion, GS232B_MOTION_ALL_AXES, targets) == GS232B_OK);
    esp_adc_fake.now_us = 10;
    CHECK(gs232b_motion_tick(&fixture.motion) == GS232B_OK);
    CHECK((fixture.shadow & 0x0fU) == 0x05U);
    esp_adc_fake.now_us = GS232B_FEEDBACK_SAMPLE_MAX_AGE_US + 1;
    CHECK(gs232b_motion_tick(&fixture.motion) == GS232B_ERROR_NOT_READY);
    gs232b_motion_snapshot_t state;
    CHECK(gs232b_motion_snapshot(&fixture.motion, &state) == GS232B_OK);
    CHECK(state.fault.code == GS232B_FAULT_SAMPLE);
    CHECK(!state.axes[0].position_valid && !state.axes[1].position_valid);
    CHECK(!state.axes[0].sample.valid && !state.axes[1].sample.valid);
    CHECK((fixture.shadow & 0x0fU) == 0 && fixture.pwm == 0);
    CHECK(esp_adc_fake.reads[0] == 1 && esp_adc_fake.reads[1] == 1);
    CHECK(gs232b_esp_adc_close(&fixture.adc) == GS232B_OK);
    return true;
}

static bool test_missing_factory_calibration_prevents_motion(void)
{
    esp_adc_fake_reset();
    fixture_t fixture = {0};
    esp_adc_fake.calibration_create_error[0] = ESP_ERR_NOT_SUPPORTED;
    CHECK(gs232b_esp_adc_open(&fixture.adc) == GS232B_ERROR_NOT_READY);
    CHECK(init_motion(&fixture));
    CHECK(gs232b_motion_manual(&fixture.motion, GS232B_AXIS_AZIMUTH,
                                GS232B_MOTION_POSITIVE) == GS232B_ERROR_NOT_READY);
    CHECK(gs232b_motion_manual(&fixture.motion, GS232B_AXIS_ELEVATION,
                                GS232B_MOTION_POSITIVE) == GS232B_ERROR_NOT_READY);
    gs232b_motion_snapshot_t state;
    CHECK(gs232b_motion_snapshot(&fixture.motion, &state) == GS232B_OK);
    CHECK(!state.axes[0].position_valid && !state.axes[1].position_valid);
    CHECK((fixture.shadow & 0x0fU) == 0 && fixture.pwm == 0);
    CHECK(esp_adc_fake.reads[0] == 0 && esp_adc_fake.reads[1] == 0);
    CHECK(gs232b_esp_adc_close(&fixture.adc) == GS232B_OK);
    esp_adc_fake_reset();
    return true;
}

int main(void)
{
    if (!test_invalid_feedback_stops_both_axes() ||
        !test_stopped_producer_expires_and_stops_motion() ||
        !test_missing_factory_calibration_prevents_motion()) {
        return 1;
    }
    puts("ESP-IDF ADC invalidation reaches motion safety checks");
    return 0;
}
