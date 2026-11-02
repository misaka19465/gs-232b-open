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

#include <limits.h>
#include <pthread.h>

static bool test_configuration_and_latest_delivery(void)
{
    esp_adc_fake_reset();
    gs232b_esp_adc_t adc = {0};
    const gs232b_sampler_t sampler = gs232b_esp_adc_sampler(&adc);
    CHECK(expect_not_ready(sampler, GS232B_AXIS_AZIMUTH));
    CHECK(gs232b_esp_adc_capture(&adc) == GS232B_ERROR_NOT_READY);
    CHECK(gs232b_esp_adc_open(&adc) == GS232B_OK);
    CHECK(esp_adc_fake.unit_config.unit_id == ADC_UNIT_1);
    CHECK(esp_adc_fake.unit_config.ulp_mode == ADC_ULP_MODE_DISABLE);
    CHECK(esp_adc_fake.reads[0] == 0 && esp_adc_fake.reads[1] == 0);
    for (int channel = 0; channel < 2; ++channel) {
        CHECK(esp_adc_fake.channel_config[channel].atten == ADC_ATTEN_DB_12);
        CHECK(esp_adc_fake.channel_config[channel].bitwidth == ADC_BITWIDTH_12);
        CHECK(esp_adc_fake.calibration_config[channel].unit_id == ADC_UNIT_1);
        CHECK(esp_adc_fake.calibration_config[channel].chan == (adc_channel_t)channel);
        CHECK(esp_adc_fake.calibration_config[channel].atten == ADC_ATTEN_DB_12);
        CHECK(esp_adc_fake.calibration_config[channel].bitwidth == ADC_BITWIDTH_12);
        CHECK(esp_adc_fake.calibration_creates[channel] == 1);
    }
    CHECK(expect_not_ready(sampler, GS232B_AXIS_AZIMUTH));
    CHECK(expect_not_ready(sampler, GS232B_AXIS_ELEVATION));
    CHECK(gs232b_esp_adc_open(&adc) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_esp_adc_capture(&adc) == GS232B_OK);
    gs232b_sample_t az, el;
    CHECK(gs232b_sampler_read(&sampler, GS232B_AXIS_AZIMUTH, &az) == GS232B_OK);
    CHECK(gs232b_sampler_read(&sampler, GS232B_AXIS_ELEVATION, &el) == GS232B_OK);
    CHECK(az.valid && az.raw == 913 && az.adc_uv == 600000 && az.acquired_at_us == 0);
    CHECK(el.valid && el.raw == 1729 && el.adc_uv == 1100000 && el.acquired_at_us == 0);
    CHECK(esp_adc_fake.last_converted_raw[1] == 913);
    CHECK(esp_adc_fake.last_converted_raw[0] == 1729);

    /* NOTE: 快生产、慢消费时覆盖单槽，不积压旧样本或等待消费。 */
    for (unsigned i = 1; i <= 4; ++i) {
        esp_adc_fake.now_us = i * 5000;
        esp_adc_fake.raw[1] = 100 + (int)i;
        esp_adc_fake.voltage_mv[1] = 700 + (int)i;
        CHECK(gs232b_esp_adc_capture(&adc) == GS232B_OK);
    }
    for (unsigned i = 0; i < 20; ++i) {
        CHECK(gs232b_sampler_read(&sampler, GS232B_AXIS_AZIMUTH, &az) == GS232B_OK);
        CHECK(az.raw == 104 && az.adc_uv == 704000 && az.acquired_at_us == 20000);
    }
    CHECK(esp_adc_fake.reads[0] == 5 && esp_adc_fake.reads[1] == 5);
    CHECK(esp_adc_fake.conversions[0] == 5 && esp_adc_fake.conversions[1] == 5);
    CHECK(esp_adc_fake.queue_overwrites == 10);
    CHECK(gs232b_esp_adc_close(&adc) == GS232B_OK);
    CHECK(expect_not_ready(sampler, GS232B_AXIS_AZIMUTH));
    CHECK(gs232b_esp_adc_open(&adc) == GS232B_OK);
    CHECK(expect_not_ready(sampler, GS232B_AXIS_ELEVATION));
    CHECK(gs232b_esp_adc_close(&adc) == GS232B_OK);
    CHECK(gs232b_esp_adc_close(&adc) == GS232B_OK);
    return true;
}

static bool test_timestamps_and_expiry(void)
{
    esp_adc_fake_reset();
    gs232b_esp_adc_t adc = {0};
    CHECK(gs232b_esp_adc_open(&adc) == GS232B_OK);
    const gs232b_sampler_t sampler = gs232b_esp_adc_sampler(&adc);
    esp_adc_fake.now_us = 1000;
    esp_adc_fake.read_duration_us[1] = 20;
    esp_adc_fake.conversion_duration_us[1] = 30;
    esp_adc_fake.read_duration_us[0] = 40;
    esp_adc_fake.conversion_duration_us[0] = 50;
    CHECK(gs232b_esp_adc_capture(&adc) == GS232B_OK);
    gs232b_sample_t sample;
    CHECK(gs232b_sampler_read(&sampler, GS232B_AXIS_AZIMUTH, &sample) == GS232B_OK);
    CHECK(sample.acquired_at_us == 1020);
    CHECK(gs232b_sampler_read(&sampler, GS232B_AXIS_ELEVATION, &sample) == GS232B_OK);
    CHECK(sample.acquired_at_us == 1090);
    esp_adc_fake.now_us = 101020;
    CHECK(gs232b_sampler_read(&sampler, GS232B_AXIS_AZIMUTH, &sample) == GS232B_OK);
    CHECK(sample.acquired_at_us == 1020);
    ++esp_adc_fake.now_us;
    CHECK(expect_not_ready(sampler, GS232B_AXIS_AZIMUTH));
    CHECK(gs232b_sampler_read(&sampler, GS232B_AXIS_ELEVATION, &sample) == GS232B_OK);
    esp_adc_fake.now_us = 101091;
    CHECK(expect_not_ready(sampler, GS232B_AXIS_ELEVATION));
    esp_adc_fake.now_us = 1000;
    CHECK(expect_not_ready(sampler, GS232B_AXIS_AZIMUTH));
    esp_adc_fake.now_us = -1;
    CHECK(expect_not_ready(sampler, GS232B_AXIS_ELEVATION));
    memset(esp_adc_fake.read_duration_us, 0, sizeof(esp_adc_fake.read_duration_us));
    CHECK(gs232b_esp_adc_capture(&adc) == GS232B_ERROR_NOT_READY);
    CHECK(expect_not_ready(sampler, GS232B_AXIS_AZIMUTH));
    CHECK(expect_not_ready(sampler, GS232B_AXIS_ELEVATION));
    esp_adc_fake.now_us = 200000;
    /* NOTE: 即使校准返回成功，耗时超过新鲜度预算也不提供陈旧电压。 */
    esp_adc_fake.conversion_duration_us[0] = 100001;
    esp_adc_fake.conversion_duration_us[1] = 100001;
    CHECK(gs232b_esp_adc_capture(&adc) == GS232B_OK);
    CHECK(expect_not_ready(sampler, GS232B_AXIS_AZIMUTH));
    CHECK(expect_not_ready(sampler, GS232B_AXIS_ELEVATION));
    CHECK(gs232b_esp_adc_close(&adc) == GS232B_OK);
    return true;
}

static bool test_failed_capture_invalidates_only_its_axis(void)
{
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        for (int failure = 0; failure < 9; ++failure) {
            esp_adc_fake_reset();
            gs232b_esp_adc_t adc = {0};
            CHECK(gs232b_esp_adc_open(&adc) == GS232B_OK);
            const gs232b_sampler_t sampler = gs232b_esp_adc_sampler(&adc);
            CHECK(gs232b_esp_adc_capture(&adc) == GS232B_OK);
            const int channel = axis == GS232B_AXIS_AZIMUTH ? 1 : 0;
            switch (failure) {
            case 0: esp_adc_fake.read_error[channel] = ESP_ERR_TIMEOUT; break;
            case 1: esp_adc_fake.read_error[channel] = ESP_FAIL; break;
            case 2: esp_adc_fake.convert_error[channel] = ESP_ERR_INVALID_STATE; break;
            case 3: esp_adc_fake.convert_error[channel] = ESP_FAIL; break;
            case 4: esp_adc_fake.raw[channel] = -1; break;
            case 5: esp_adc_fake.raw[channel] = 4096; break;
            case 6: esp_adc_fake.voltage_mv[channel] = -1; break;
            case 7: esp_adc_fake.voltage_mv[channel] = 2501; break;
            default: esp_adc_fake.voltage_mv[channel] = INT_MAX; break;
            }
            esp_adc_fake.now_us = 5000;
            CHECK(gs232b_esp_adc_capture(&adc) == GS232B_ERROR_NOT_READY);
            CHECK(expect_not_ready(sampler, (gs232b_axis_id_t)axis));
            gs232b_sample_t sample;
            CHECK(gs232b_sampler_read(&sampler, (gs232b_axis_id_t)(1 - axis),
                                      &sample) == GS232B_OK);
            CHECK(sample.acquired_at_us == 5000);
            CHECK(esp_adc_fake.reads[0] == 2 && esp_adc_fake.reads[1] == 2);
            if (failure == 0 || failure == 1 || failure == 4 || failure == 5) {
                CHECK(esp_adc_fake.conversions[channel] == 1);
            }
            esp_adc_fake.read_error[channel] = ESP_OK;
            esp_adc_fake.convert_error[channel] = ESP_OK;
            esp_adc_fake.raw[channel] = 0;
            esp_adc_fake.voltage_mv[channel] = 0;
            CHECK(expect_not_ready(sampler, (gs232b_axis_id_t)axis));
            ++esp_adc_fake.now_us;
            CHECK(gs232b_esp_adc_capture(&adc) == GS232B_OK);
            CHECK(gs232b_sampler_read(&sampler, (gs232b_axis_id_t)axis, &sample) == GS232B_OK);
            CHECK(sample.valid && sample.adc_uv == 0 && sample.raw == 0);
            esp_adc_fake.raw[channel] = 4095;
            esp_adc_fake.voltage_mv[channel] = 2500;
            CHECK(gs232b_esp_adc_capture(&adc) == GS232B_OK);
            CHECK(gs232b_sampler_read(&sampler, (gs232b_axis_id_t)axis, &sample) == GS232B_OK);
            CHECK(sample.raw == 4095 && sample.adc_uv == 2500000);
            CHECK(gs232b_feedback_sample_validate(&sample, sample.acquired_at_us) ==
                  GS232B_ERROR_HARDWARE_FAULT);
            CHECK(gs232b_esp_adc_close(&adc) == GS232B_OK);
        }
    }
    return true;
}

static bool test_initialization_failure_and_cleanup(void)
{
    for (int failure = 0; failure < 9; ++failure) {
        esp_adc_fake_reset();
        gs232b_esp_adc_t adc = {0};
        gs232b_error_t expected = GS232B_ERROR_NOT_READY;
        switch (failure) {
        case 0:
            esp_adc_fake.unit_create_error = ESP_ERR_NO_MEM;
            expected = GS232B_ERROR_RESOURCE_EXHAUSTED;
            break;
        case 1:
            esp_adc_fake.unit_create_error = ESP_ERR_NOT_FOUND;
            expected = GS232B_ERROR_BUSY;
            break;
        case 2: case 3:
            esp_adc_fake.config_error[failure - 2] = ESP_FAIL;
            expected = GS232B_ERROR_IO;
            break;
        case 4: case 5:
            esp_adc_fake.calibration_create_error[failure - 4] = ESP_ERR_NOT_SUPPORTED;
            break;
        case 6: esp_adc_fake.calibration_create_error[0] = ESP_ERR_NO_MEM; break;
        default:
            esp_adc_fake.fail_queue_create_at = (unsigned)(failure - 6);
            expected = GS232B_ERROR_RESOURCE_EXHAUSTED;
            break;
        }
        CHECK(gs232b_esp_adc_open(&adc) == expected);
        const gs232b_sampler_t sampler = gs232b_esp_adc_sampler(&adc);
        CHECK(expect_not_ready(sampler, GS232B_AXIS_AZIMUTH));
        CHECK(expect_not_ready(sampler, GS232B_AXIS_ELEVATION));
        CHECK(gs232b_esp_adc_capture(&adc) == GS232B_ERROR_NOT_READY);
        CHECK(!esp_adc_fake.unit_live);
        CHECK(!esp_adc_fake.calibration_live[0] && !esp_adc_fake.calibration_live[1]);
        CHECK(esp_adc_fake.queue_allocations == esp_adc_fake.queue_deletes);
        CHECK(esp_adc_fake.reads[0] == 0 && esp_adc_fake.reads[1] == 0);
        esp_adc_fake.unit_create_error = ESP_OK;
        memset(esp_adc_fake.config_error, 0, sizeof(esp_adc_fake.config_error));
        memset(esp_adc_fake.calibration_create_error, 0,
               sizeof(esp_adc_fake.calibration_create_error));
        esp_adc_fake.fail_queue_create_at = 0;
        CHECK(gs232b_esp_adc_open(&adc) == GS232B_OK);
        CHECK(expect_not_ready(sampler, GS232B_AXIS_AZIMUTH));
        CHECK(gs232b_esp_adc_capture(&adc) == GS232B_OK);
        CHECK(gs232b_esp_adc_close(&adc) == GS232B_OK);
    }
    esp_adc_fake_reset();
    return true;
}

static bool test_failed_close_keeps_samples_unavailable(void)
{
    for (int failure = 0; failure < 3; ++failure) {
        esp_adc_fake_reset();
        gs232b_esp_adc_t adc = {0};
        CHECK(gs232b_esp_adc_open(&adc) == GS232B_OK);
        CHECK(gs232b_esp_adc_capture(&adc) == GS232B_OK);
        const gs232b_sampler_t sampler = gs232b_esp_adc_sampler(&adc);
        if (failure == 2) {
            esp_adc_fake.unit_delete_error = ESP_FAIL;
        } else {
            esp_adc_fake.calibration_delete_error[failure] = ESP_FAIL;
        }
        CHECK(gs232b_esp_adc_close(&adc) == GS232B_ERROR_IO);
        CHECK(expect_not_ready(sampler, GS232B_AXIS_AZIMUTH));
        CHECK(expect_not_ready(sampler, GS232B_AXIS_ELEVATION));
        CHECK(gs232b_esp_adc_capture(&adc) == GS232B_ERROR_NOT_READY);
        CHECK(gs232b_esp_adc_open(&adc) == GS232B_ERROR_INVALID_ARGUMENT);
        esp_adc_fake.unit_delete_error = ESP_OK;
        memset(esp_adc_fake.calibration_delete_error, 0,
               sizeof(esp_adc_fake.calibration_delete_error));
        CHECK(gs232b_esp_adc_close(&adc) == GS232B_OK);
        CHECK(gs232b_esp_adc_open(&adc) == GS232B_OK);
        CHECK(expect_not_ready(sampler, GS232B_AXIS_AZIMUTH));
        CHECK(gs232b_esp_adc_close(&adc) == GS232B_OK);
    }
    return true;
}

static bool test_failed_open_cleanup_can_be_retried(void)
{
    esp_adc_fake_reset();
    gs232b_esp_adc_t adc = {0};
    esp_adc_fake.calibration_create_error[0] = ESP_ERR_NOT_SUPPORTED;
    esp_adc_fake.calibration_delete_error[1] = ESP_FAIL;
    CHECK(gs232b_esp_adc_open(&adc) == GS232B_ERROR_NOT_READY);
    CHECK(esp_adc_fake.unit_live && esp_adc_fake.calibration_live[1]);
    const gs232b_sampler_t sampler = gs232b_esp_adc_sampler(&adc);
    CHECK(expect_not_ready(sampler, GS232B_AXIS_AZIMUTH));
    CHECK(expect_not_ready(sampler, GS232B_AXIS_ELEVATION));
    CHECK(gs232b_esp_adc_open(&adc) == GS232B_ERROR_INVALID_ARGUMENT);
    esp_adc_fake.calibration_delete_error[1] = ESP_OK;
    CHECK(gs232b_esp_adc_close(&adc) == GS232B_OK);
    esp_adc_fake_reset();
    return true;
}

static void *capture_thread(void *ctx)
{
    gs232b_esp_adc_t *adc = ctx;
    for (unsigned i = 0; i < 20000; ++i) {
        esp_adc_fake.raw[1] = (i & 1U) ? 913 : 1729;
        esp_adc_fake.voltage_mv[1] = (i & 1U) ? 600 : 1100;
        ++esp_adc_fake.now_us;
        if (gs232b_esp_adc_capture(adc) != GS232B_OK) { return adc; }
    }
    return NULL;
}

static bool test_concurrent_mailbox_reads_are_coherent(void)
{
    esp_adc_fake_reset();
    gs232b_esp_adc_t adc = {0};
    CHECK(gs232b_esp_adc_open(&adc) == GS232B_OK);
    CHECK(gs232b_esp_adc_capture(&adc) == GS232B_OK);
    const gs232b_sampler_t sampler = gs232b_esp_adc_sampler(&adc);
    pthread_t producer;
    CHECK(pthread_create(&producer, NULL, capture_thread, &adc) == 0);
    bool coherent = true;
    gs232b_time_us_t last_at_us = 0;
    for (unsigned i = 0; i < 40000; ++i) {
        gs232b_sample_t sample;
        if (gs232b_sampler_read(&sampler, GS232B_AXIS_AZIMUTH, &sample) != GS232B_OK ||
            !((sample.raw == 913 && sample.adc_uv == 600000) ||
              (sample.raw == 1729 && sample.adc_uv == 1100000)) ||
            sample.acquired_at_us < last_at_us) {
            coherent = false;
            break;
        }
        last_at_us = sample.acquired_at_us;
    }
    void *result = NULL;
    CHECK(pthread_join(producer, &result) == 0);
    CHECK(result == NULL && coherent);
    CHECK(gs232b_esp_adc_close(&adc) == GS232B_OK);
    esp_adc_fake_reset();
    return true;
}

int main(void)
{
    if (!test_configuration_and_latest_delivery() || !test_timestamps_and_expiry() ||
        !test_failed_capture_invalidates_only_its_axis() ||
        !test_initialization_failure_and_cleanup() ||
        !test_failed_close_keeps_samples_unavailable() ||
        !test_failed_open_cleanup_can_be_retried() ||
        !test_concurrent_mailbox_reads_are_coherent()) {
        return 1;
    }
    puts("ESP-IDF ADC API substitute tests passed");
    return 0;
}
