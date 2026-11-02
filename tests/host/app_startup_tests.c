#include "app_test_support.h"

static bool startup_trace_and_network_failure(void)
{
    app_fixture_t f;
    CHECK(fixture_init(&f, false));
    f.network_error = GS232B_ERROR_IO;
    f.boot_error = GS232B_ERROR_WOULD_BLOCK;
    CHECK(start(&f, false));
    CHECK(f.spi.latched == 0x00 && f.spi.pwm == 0);
    bool latch = false, pwm = false;
    for (size_t i = 0; i < f.spi.count; ++i) {
        if (f.spi.trace[i].kind == SPI_LATCH_HIGH) {
            CHECK(f.spi.trace[i].value == 0x00);
            latch = true;
        }
        if (f.spi.trace[i].kind == SPI_PWM) {
            CHECK(f.spi.trace[i].value == 0);
            pwm = true;
        }
    }
    CHECK(latch && pwm && f.store_opens == 0 && f.adc_opens == 0 && f.uart_opens == 0);
    advance(&f, GS232B_W5500_RESET_DEFAULT_US - 1);
    CHECK(gs232b_app_step(&f.app) == GS232B_OK && f.spi.latched == 0);
    advance(&f, 1);
    CHECK(gs232b_app_step(&f.app) == GS232B_OK && f.spi.latched == 0x10 && f.spi.pwm == 0);
    CHECK(f.store_opens == 0 && f.adc_opens == 0 && f.uart_opens == 0);
    advance(&f, GS232B_W5500_RECOVERY_DEFAULT_US - 1);
    CHECK(gs232b_app_step(&f.app) == GS232B_OK && f.store_opens == 0);
    advance(&f, 1);
    CHECK(gs232b_app_step(&f.app) == GS232B_OK);
    CHECK(f.store_opens == 1 && f.adc_opens == 1 && f.uart_opens == 1 && f.safe_order);
    CHECK(gs232b_app_step(&f.app) == GS232B_OK);
    CHECK(f.attaches == 0 && f.network_starts == 0);
    f.boot_error = GS232B_OK;
    CHECK(gs232b_app_step(&f.app) == GS232B_OK);
    CHECK(f.app.status.uart_ready && f.app.status.network_error == GS232B_ERROR_IO);
    CHECK(f.attaches == 1 && f.uart_services == 2 && f.network_starts == 1 && f.safe_order);
    CHECK(expect(&f, "Connect OK\r\n"));
    CHECK(command(&f, "!SYS INFO\r"));
    CHECK(gs232b_app_step(&f.app) == GS232B_OK);
    CHECK(expect(&f, "\"uart1\":true"));
    CHECK(f.uart_services == 4);
    gs232b_fake_spi_bus_destroy(&f.spi);
    return true;
}

static bool readiness_gates_and_calibration(void)
{
    for (unsigned missing = 0; missing < 3; ++missing) {
        app_fixture_t f;
        CHECK(fixture_init(&f, missing != 0));
        if (missing == 1) f.sample_error = f.adc_error = GS232B_ERROR_NOT_READY;
        CHECK(boot(&f, missing != 2));
        gs232b_motion_snapshot_t state;
        CHECK(gs232b_motion_snapshot(&f.app.motion, &state) == GS232B_OK);
        for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
            CHECK(state.axes[i].zero_valid == (missing != 0));
            CHECK(state.axes[i].sample.valid == (missing != 1));
            CHECK(state.axes[i].position_valid == (missing == 2));
            CHECK(state.axes[i].mode == (missing == 1 ? GS232B_AXIS_IDLE : GS232B_AXIS_DISABLED));
            CHECK(gs232b_motion_manual(&f.app.motion, (gs232b_axis_id_t)i,
                GS232B_MOTION_POSITIVE) == GS232B_ERROR_NOT_READY);
        }
        const int32_t targets[] = {180000, 90000};
        CHECK(gs232b_motion_goto(&f.app.motion, GS232B_MOTION_ALL_AXES, targets) == GS232B_ERROR_NOT_READY);
        CHECK(command(&f, "!MOTION STATUS\r"));
        CHECK(gs232b_app_step(&f.app) == GS232B_OK);
        CHECK(expect(&f, "\"motion_ready\":false"));
        CHECK(command(&f, "R\r"));
        CHECK(gs232b_app_step(&f.app) == GS232B_ERROR_NOT_READY);
        CHECK(expect(&f, "?>\r\n"));
        CHECK(f.spi.latched == 0x10 && f.spi.pwm == 0);
        if (missing == 2) {
            CHECK(command(&f, "O\r"));
            CHECK(gs232b_app_step(&f.app) == GS232B_OK);
            CHECK(command(&f, "Y\r"));
            CHECK(gs232b_app_step(&f.app) == GS232B_OK);
            CHECK(f.app.motion.config.axes[0].zero_uv == 500000);
            CHECK(!f.app.motion.config.parameters_verified && f.spi.pwm == 0);
        }
        gs232b_fake_spi_bus_destroy(&f.spi);
    }
    return true;
}

static bool output_and_storage_failures(void)
{
    for (unsigned release = 0; release < 2; ++release) {
        app_fixture_t f;
        CHECK(fixture_init(&f, true));
        if (release == 0) gs232b_fake_spi_bus_fault(&f.spi, SPI_SHIFT_BYTE, 0, GS232B_ERROR_IO);
        const bool started = start(&f, true);
        if (release != 0) {
            CHECK(started);
            gs232b_fake_spi_bus_fault(&f.spi, SPI_SHIFT_BYTE, 0, GS232B_ERROR_IO);
            advance(&f, GS232B_W5500_RESET_DEFAULT_US);
            CHECK(gs232b_app_step(&f.app) == GS232B_ERROR_IO);
        } else CHECK(!started);
        CHECK(f.app.status.phase == GS232B_APP_FAILED && !f.app.outputs.state.output.known);
        CHECK(f.store_opens == 0 && f.adc_opens == 0 && f.uart_opens == 0);
        const size_t attempts = f.spi.count;
        CHECK(gs232b_app_step(&f.app) == GS232B_ERROR_IO && f.spi.count == attempts);
        gs232b_fake_spi_bus_destroy(&f.spi);
    }
    app_fixture_t f;
    CHECK(fixture_init(&f, true));
    gs232b_fake_fail_next(&f.store.failures[GS232B_STORE_DISPLAY][GS232B_FAKE_STORE_LOAD],
        GS232B_ERROR_CORRUPT, 1);
    CHECK(start(&f, true));
    advance(&f, GS232B_W5500_RESET_DEFAULT_US);
    CHECK(gs232b_app_step(&f.app) == GS232B_OK);
    advance(&f, GS232B_W5500_RECOVERY_DEFAULT_US);
    CHECK(gs232b_app_step(&f.app) == GS232B_ERROR_CORRUPT);
    CHECK(f.app.status.phase == GS232B_APP_FAILED && f.spi.pwm == 0);
    CHECK(f.uart_opens == 0 && f.network_starts == 0);
    gs232b_fake_spi_bus_destroy(&f.spi);
    return true;
}

static bool verified_boot_allows_fresh_motion(void)
{
    app_fixture_t f;
    CHECK(fixture_init(&f, true) && boot(&f, true));
    CHECK(command(&f, "R\r"));
    CHECK(gs232b_app_step(&f.app) == GS232B_OK);
    advance(&f, 250000);
    CHECK(gs232b_app_step(&f.app) == GS232B_OK);
    CHECK(f.spi.latched == 0x14 && f.spi.pwm > 0);
    CHECK(command(&f, "S\r"));
    CHECK(gs232b_app_step(&f.app) == GS232B_OK);
    CHECK(f.spi.latched == 0x10 && f.spi.pwm == 0);
    gs232b_fake_spi_bus_destroy(&f.spi);
    return true;
}

static bool unavailable_store_keeps_uart_and_motion_gate(void)
{
    app_fixture_t f;
    CHECK(fixture_init(&f, false));
    f.store_error = GS232B_ERROR_IO;
    CHECK(boot(&f, true));
    CHECK(f.app.status.store_error == GS232B_ERROR_IO && f.app.status.uart_ready);
    CHECK(!f.app.motion.config.parameters_verified);
    CHECK(f.app.status.zero_error[0] == GS232B_ERROR_NOT_READY);
    CHECK(command(&f, "!MOTION STATUS\r"));
    CHECK(gs232b_app_step(&f.app) == GS232B_OK);
    CHECK(expect(&f, "\"parameters_verified\":false"));
    CHECK(f.spi.latched == 0x10 && f.spi.pwm == 0);
    gs232b_fake_spi_bus_destroy(&f.spi);
    return true;
}

int main(void)
{
    if (!startup_trace_and_network_failure() || !readiness_gates_and_calibration() ||
        !output_and_storage_failures() || !verified_boot_allows_fresh_motion() ||
        !unavailable_store_keeps_uart_and_motion_gate()) return 1;
    puts("app startup order, reset deadlines, UART isolation and motion gates passed");
    return 0;
}
