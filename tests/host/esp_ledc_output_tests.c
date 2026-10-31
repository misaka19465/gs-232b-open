#include "gs232b/esp_ledc.h"
#include "gs232b/spi_bus.h"
#include "ledc_api.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); return false; \
} } while (0)

/* NOTE: 原生测试只替换 IDF 与 SPI IO，使用真实输出管理器、共享提交和 LEDC。 */
static gs232b_esp_ledc_t pwm;

static gs232b_error_t set_pwm(void *ctx, uint16_t permille)
{
    gs232b_fake_spi_bus_t *fake = ctx;
    const gs232b_error_t error = gs232b_fake_spi_bus_event(fake, SPI_PWM, permille);
    return error != GS232B_OK ? error : gs232b_esp_ledc_set_permille(&pwm, permille);
}

static bool test_requests_submissions_and_voltage_confirmation(void)
{
    esp_ledc_fake_reset();
    gs232b_fake_spi_bus_t fake;
    gs232b_shared_spi_t bus;
    gs232b_output_manager_t manager;
    CHECK(gs232b_fake_spi_bus_init(&fake, &bus));
    bus.io.set_pwm = set_pwm;
    CHECK(gs232b_esp_ledc_open(&pwm) == GS232B_OK);
    CHECK(gs232b_output_manager_init(&manager,
        (gs232b_clock_t){.ctx = &fake, .now_us = gs232b_fake_spi_bus_now},
        gs232b_shared_spi_output_driver(&bus), 1000) == GS232B_OK);
    gs232b_output_manager_snapshot_t state;
    CHECK(gs232b_output_manager_snapshot(&manager, &state) == GS232B_OK);
    CHECK(state.output.known && !state.output.speed_voltage_confirmed);
    CHECK(esp_ledc_fake.submitted_duty == 0 && fake.latched == 0);

    const gs232b_output_frame_t forward = {
        .azimuth = GS232B_AZ_CW, .elevation = GS232B_EL_UP,
        .speed_permille = 1000, .w5500_reset_asserted = false
    };
    CHECK(gs232b_output_manager_apply(&manager, &forward) == GS232B_OK);
    CHECK(gs232b_output_manager_snapshot(&manager, &state) == GS232B_OK);
    CHECK(state.requested.speed_permille == 1000 && state.reversal_waiting[0]);
    CHECK(state.output.last_successful.speed_permille == 0);
    CHECK(esp_ledc_fake.submitted_duty == 0);
    atomic_fetch_add(&fake.now_us, 1000);
    CHECK(gs232b_output_manager_tick(&manager) == GS232B_OK);
    CHECK(gs232b_output_manager_snapshot(&manager, &state) == GS232B_OK);
    CHECK(state.output.known && !state.output.speed_voltage_confirmed);
    CHECK(state.output.last_successful.speed_permille == 1000);
    CHECK(esp_ledc_fake.submitted_duty == 1024 && fake.latched == 0x15);
    CHECK(gs232b_output_manager_set_pwm(&manager, 0) == GS232B_OK);
    CHECK(esp_ledc_fake.submitted_duty == 0 && fake.latched == 0x15);

    /* NOTE: update 错误可已改变计数；保留成功请求，仅将当前提交状态标为未知。 */
    esp_ledc_fake.fail_next[LEDC_FAKE_UPDATE] = ESP_FAIL;
    CHECK(gs232b_output_manager_set_pwm(&manager, 1000) == GS232B_ERROR_IO);
    CHECK(gs232b_output_manager_snapshot(&manager, &state) == GS232B_OK);
    CHECK(!state.output.known && !state.output.speed_voltage_confirmed);
    CHECK(state.output.requested.speed_permille == 1000);
    CHECK(state.output.last_successful.speed_permille == 0);
    CHECK(state.requested.speed_permille == 0 && state.requested.azimuth == GS232B_AZ_STOP);
    CHECK(esp_ledc_fake.submitted_duty == 1024);
    CHECK(gs232b_output_manager_tick(&manager) == GS232B_ERROR_NOT_READY);
    CHECK(gs232b_output_manager_stop_all(&manager) == GS232B_OK);
    CHECK(esp_ledc_fake.submitted_duty == 0 && fake.latched == 0x10);
    CHECK(gs232b_output_manager_snapshot(&manager, &state) == GS232B_OK);
    CHECK(state.output.known && !state.output.speed_voltage_confirmed);

    /* NOTE: PWM 降零失败不能阻止 595 清方向，也不能掩盖驱动错误。 */
    CHECK(gs232b_output_manager_apply(&manager, &forward) == GS232B_OK);
    atomic_fetch_add(&fake.now_us, 1000);
    CHECK(gs232b_output_manager_tick(&manager) == GS232B_OK);
    CHECK(fake.latched == 0x15 && esp_ledc_fake.submitted_duty == 1024);
    esp_ledc_fake.fail_next[LEDC_FAKE_SET] = ESP_FAIL;
    CHECK(gs232b_output_manager_stop_all(&manager) == GS232B_ERROR_IO);
    CHECK(fake.latched == 0x10 && esp_ledc_fake.submitted_duty == 1024);
    CHECK(gs232b_output_manager_snapshot(&manager, &state) == GS232B_OK);
    CHECK(!state.output.known && !state.output.speed_voltage_confirmed);
    CHECK(gs232b_output_manager_stop_all(&manager) == GS232B_OK);
    CHECK(esp_ledc_fake.submitted_duty == 0);
    CHECK(atomic_load(&fake.safe) && !esp_ledc_fake.invalid_call);
    CHECK(gs232b_esp_ledc_close(&pwm) == GS232B_OK);
    gs232b_fake_spi_bus_destroy(&fake);
    return true;
}

int main(void)
{
    return test_requests_submissions_and_voltage_confirmation() ? EXIT_SUCCESS : EXIT_FAILURE;
}
