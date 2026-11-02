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
#include "ledc_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); return false; \
} } while (0)

static bool test_configuration_and_all_duties(void)
{
    esp_ledc_fake_reset();
    gs232b_esp_ledc_t pwm = {0};
    CHECK(gs232b_esp_ledc_set_permille(&pwm, 0) == GS232B_ERROR_NOT_READY);
    CHECK(esp_ledc_fake.calls[LEDC_FAKE_SET] == 0);
    CHECK(gs232b_esp_ledc_open(&pwm) == GS232B_OK);
    CHECK(esp_ledc_fake.timer.freq_hz == 20000);
    CHECK(esp_ledc_fake.timer.duty_resolution == LEDC_TIMER_10_BIT);
    CHECK(esp_ledc_fake.channel.gpio_num == 20);
    CHECK(esp_ledc_fake.channel.duty == 0 && esp_ledc_fake.channel.hpoint == 0);
    CHECK(!esp_ledc_fake.channel.flags.output_invert);
    CHECK(gs232b_esp_ledc_open(&pwm) == GS232B_ERROR_INVALID_ARGUMENT);
    uint32_t previous = 0;
    for (uint16_t permille = 0; permille <= 1000; ++permille) {
        uint32_t duty = UINT32_MAX;
        CHECK(gs232b_esp_ledc_duty_from_permille(permille, &duty) == GS232B_OK);
        CHECK(duty >= previous && duty <= 1024);
        /* NOTE: 四舍五入误差不超过半个计数，独立于实现的整数表达式。 */
        const int error = (int)(duty * 1000U) - (int)((uint32_t)permille * 1024U);
        CHECK(error >= -500 && error <= 500);
        CHECK(gs232b_esp_ledc_set_permille(&pwm, permille) == GS232B_OK);
        CHECK(esp_ledc_fake.submitted_duty == duty);
        previous = duty;
    }
    CHECK(esp_ledc_fake.submitted_duty == 1024);
    CHECK(gs232b_esp_ledc_set_permille(&pwm, 0) == GS232B_OK);
    CHECK(esp_ledc_fake.submitted_duty == 0);
    for (uint32_t duty = 0; duty <= 1024; ++duty) {
        CHECK(gs232b_esp_ledc_set_duty(&pwm, duty) == GS232B_OK);
        CHECK(esp_ledc_fake.submitted_duty == duty);
    }
    CHECK(!esp_ledc_fake.invalid_call);
    CHECK(gs232b_esp_ledc_close(&pwm) == GS232B_OK);
    CHECK(!pwm.opened && !pwm.channel_open && !pwm.timer_open);
    CHECK(!esp_ledc_fake.timer_open && !esp_ledc_fake.channel_open);
    CHECK(esp_ledc_fake.submitted_duty == 0);
    CHECK(gs232b_esp_ledc_close(&pwm) == GS232B_OK);
    CHECK(gs232b_esp_ledc_set_duty(&pwm, 0) == GS232B_ERROR_NOT_READY);
    CHECK(gs232b_esp_ledc_open(&pwm) == GS232B_OK);
    CHECK(esp_ledc_fake.submitted_duty == 0);
    CHECK(gs232b_esp_ledc_close(&pwm) == GS232B_OK);
    return true;
}

static bool test_invalid_counts_never_reach_idf(void)
{
    esp_ledc_fake_reset();
    gs232b_esp_ledc_t pwm = {0};
    CHECK(gs232b_esp_ledc_open(&pwm) == GS232B_OK);
    CHECK(gs232b_esp_ledc_set_duty(&pwm, 512) == GS232B_OK);
    const esp_ledc_fake_t before = esp_ledc_fake;
    const uint32_t counts[] = {1025, 2048, UINT16_MAX, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
        CHECK(gs232b_esp_ledc_set_duty(&pwm, counts[i]) == GS232B_ERROR_INVALID_ARGUMENT);
    }
    const uint16_t permilles[] = {1001, 1024, UINT16_MAX};
    for (unsigned i = 0; i < sizeof(permilles) / sizeof(permilles[0]); ++i) {
        uint32_t duty = UINT32_MAX;
        CHECK(gs232b_esp_ledc_duty_from_permille(permilles[i], &duty) ==
            GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(duty == 0);
        CHECK(gs232b_esp_ledc_set_permille(&pwm, permilles[i]) ==
            GS232B_ERROR_INVALID_ARGUMENT);
    }
    CHECK(gs232b_esp_ledc_duty_from_permille(0, NULL) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_esp_ledc_set_duty(NULL, 0) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_esp_ledc_set_permille(NULL, 0) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_esp_ledc_open(NULL) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_esp_ledc_close(NULL) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(memcmp(&before, &esp_ledc_fake, sizeof(before)) == 0);
    CHECK(gs232b_esp_ledc_close(&pwm) == GS232B_OK);
    return true;
}

static bool test_write_errors_are_not_success(void)
{
    const esp_err_t errors[] = {ESP_FAIL, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT,
                                ESP_ERR_NO_MEM, ESP_ERR_INVALID_ARG};
    const gs232b_error_t mapped[] = {GS232B_ERROR_IO, GS232B_ERROR_NOT_READY,
        GS232B_ERROR_TIMEOUT, GS232B_ERROR_RESOURCE_EXHAUSTED,
        GS232B_ERROR_INVALID_ARGUMENT};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        for (ledc_fake_op_t op = LEDC_FAKE_SET; op <= LEDC_FAKE_UPDATE; ++op) {
            esp_ledc_fake_reset();
            gs232b_esp_ledc_t pwm = {0};
            CHECK(gs232b_esp_ledc_open(&pwm) == GS232B_OK);
            esp_ledc_fake.fail_next[op] = errors[i];
            CHECK(gs232b_esp_ledc_set_permille(&pwm, 1000) == mapped[i]);
            CHECK(esp_ledc_fake.calls[LEDC_FAKE_SET] == 1);
            CHECK(esp_ledc_fake.calls[LEDC_FAKE_UPDATE] == (op == LEDC_FAKE_SET ? 0U : 1U));
            CHECK(gs232b_esp_ledc_set_permille(&pwm, 0) == GS232B_OK);
            CHECK(esp_ledc_fake.submitted_duty == 0);
            CHECK(gs232b_esp_ledc_close(&pwm) == GS232B_OK);
        }
    }
    return true;
}

static bool test_resource_failure_and_retry(void)
{
    for (ledc_fake_op_t op = LEDC_FAKE_TIMER_OPEN; op <= LEDC_FAKE_CHANNEL_OPEN; ++op) {
        esp_ledc_fake_reset();
        gs232b_esp_ledc_t pwm = {0};
        esp_ledc_fake.fail_next[op] = ESP_FAIL;
        CHECK(gs232b_esp_ledc_open(&pwm) == GS232B_ERROR_IO);
        CHECK(!pwm.opened && !pwm.timer_open && !pwm.channel_open);
        CHECK(!esp_ledc_fake.timer_open && !esp_ledc_fake.channel_open);
        CHECK(gs232b_esp_ledc_set_permille(&pwm, 1000) == GS232B_ERROR_NOT_READY);
        CHECK(gs232b_esp_ledc_open(&pwm) == GS232B_OK);
        CHECK(gs232b_esp_ledc_close(&pwm) == GS232B_OK);
        CHECK(!esp_ledc_fake.invalid_call);
    }
    for (ledc_fake_op_t op = LEDC_FAKE_STOP; op <= LEDC_FAKE_TIMER_CLOSE; ++op) {
        esp_ledc_fake_reset();
        gs232b_esp_ledc_t pwm = {0};
        CHECK(gs232b_esp_ledc_open(&pwm) == GS232B_OK);
        esp_ledc_fake.fail_next[op] = ESP_FAIL;
        CHECK(gs232b_esp_ledc_close(&pwm) == GS232B_ERROR_IO);
        CHECK(!pwm.opened);
        CHECK(gs232b_esp_ledc_set_duty(&pwm, 0) == GS232B_ERROR_NOT_READY);
        CHECK(gs232b_esp_ledc_open(&pwm) == GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(gs232b_esp_ledc_close(&pwm) == GS232B_OK);
        CHECK(gs232b_esp_ledc_open(&pwm) == GS232B_OK);
        CHECK(gs232b_esp_ledc_close(&pwm) == GS232B_OK);
        CHECK(!esp_ledc_fake.invalid_call);
    }
    return true;
}

int main(void)
{
    if (!test_configuration_and_all_duties() || !test_invalid_counts_never_reach_idf() ||
        !test_write_errors_are_not_success() || !test_resource_failure_and_retry()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
