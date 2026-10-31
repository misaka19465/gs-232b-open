#include "gs232b/spi_bus.h"
#include "gs232b/w5500_reset.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

#define REVERSAL_US UINT64_C(100)

typedef struct {
    gs232b_fake_spi_bus_t fake;
    gs232b_shared_spi_t bus;
    gs232b_output_manager_t outputs;
    gs232b_w5500_reset_t reset;
    unsigned quiesce_calls;
    unsigned resume_calls;
    gs232b_error_t quiesce_error;
    gs232b_error_t resume_error;
    bool callbacks_unlocked;
} fixture_t;

static bool bus_unlocked(fixture_t *fixture)
{
    if (pthread_mutex_trylock(&fixture->fake.entry) != 0) { return false; }
    pthread_mutex_unlock(&fixture->fake.entry);
    return true;
}

static gs232b_error_t quiesce(void *ctx)
{
    fixture_t *fixture = ctx;
    ++fixture->quiesce_calls;
    fixture->callbacks_unlocked &= bus_unlocked(fixture);
    (void)gs232b_fake_spi_bus_event(&fixture->fake, SPI_QUIESCE, 0);
    return fixture->quiesce_error;
}

static gs232b_error_t resume(void *ctx)
{
    fixture_t *fixture = ctx;
    ++fixture->resume_calls;
    fixture->callbacks_unlocked &= bus_unlocked(fixture);
    (void)gs232b_fake_spi_bus_event(&fixture->fake, SPI_RESUME, 0);
    uint8_t version;
    const gs232b_error_t error = gs232b_shared_spi_w5500_transfer(
        &fixture->bus, 0, 0x0039, NULL, &version, 1);
    if (error != GS232B_OK || version != 0xa7) { return GS232B_ERROR_IO; }
    return fixture->resume_error;
}

static bool fixture_init(fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->callbacks_unlocked = true;
    CHECK(gs232b_fake_spi_bus_init(&fixture->fake, &fixture->bus));
    fixture->fake.latency_us = 7;
    CHECK(gs232b_output_manager_init(
        &fixture->outputs, (gs232b_clock_t){&fixture->fake, gs232b_fake_spi_bus_now},
        gs232b_shared_spi_output_driver(&fixture->bus), REVERSAL_US) == GS232B_OK);
    CHECK(fixture->fake.latched == 0 && fixture->fake.pwm == 0);
    CHECK(gs232b_w5500_reset_init(&fixture->reset, &fixture->outputs, &fixture->bus,
        (gs232b_w5500_lifecycle_t){fixture, quiesce, resume},
        GS232B_W5500_RESET_DEFAULT_US, GS232B_W5500_RECOVERY_DEFAULT_US) == GS232B_OK);
    gs232b_fake_spi_bus_clear(&fixture->fake);
    return true;
}

static gs232b_error_t query(fixture_t *fixture)
{
    uint8_t value;
    return gs232b_shared_spi_w5500_transfer(&fixture->bus, 0, 0, NULL, &value, 1);
}

static void advance(fixture_t *fixture, gs232b_time_us_t us)
{
    atomic_fetch_add(&fixture->fake.now_us, us);
}

static bool complete_reset(fixture_t *fixture)
{
    advance(fixture, GS232B_W5500_RESET_DEFAULT_US);
    CHECK(gs232b_w5500_reset_tick(&fixture->reset) == GS232B_OK);
    CHECK(fixture->reset.phase == GS232B_W5500_RECOVERING);
    CHECK(query(fixture) == GS232B_ERROR_NOT_READY);
    advance(fixture, GS232B_W5500_RECOVERY_DEFAULT_US);
    CHECK(gs232b_w5500_reset_tick(&fixture->reset) == GS232B_OK);
    CHECK(fixture->reset.phase == GS232B_W5500_READY);
    CHECK(fixture->callbacks_unlocked && atomic_load(&fixture->fake.safe));
    return true;
}

static bool test_boot_and_completion_relative_timing(void)
{
    fixture_t fixture;
    CHECK(fixture_init(&fixture));
    CHECK(fixture.reset.phase == GS232B_W5500_OFFLINE);
    CHECK(query(&fixture) == GS232B_ERROR_NOT_READY);
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
    CHECK(fixture.reset.phase == GS232B_W5500_RESET_LOW && fixture.quiesce_calls == 1);
    const gs232b_time_us_t low_completed = gs232b_fake_spi_bus_now(&fixture.fake);
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_ERROR_BUSY);
    advance(&fixture, GS232B_W5500_RESET_DEFAULT_US - 1U);
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    CHECK(fixture.reset.phase == GS232B_W5500_RESET_LOW && fixture.fake.latched == 0);
    advance(&fixture, 1);
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    CHECK(fixture.reset.phase == GS232B_W5500_RECOVERING && fixture.fake.latched == 0x10);
    const gs232b_time_us_t released = fixture.reset.phase_started_us;
    CHECK(released > low_completed + GS232B_W5500_RESET_DEFAULT_US);
    advance(&fixture, GS232B_W5500_RECOVERY_DEFAULT_US - 1U);
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    CHECK(fixture.resume_calls == 0 && query(&fixture) == GS232B_ERROR_NOT_READY);
    advance(&fixture, 1);
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    CHECK(fixture.resume_calls == 1 && fixture.reset.phase == GS232B_W5500_READY);
    CHECK(query(&fixture) == GS232B_OK && fixture.fake.latched == 0x10);
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    CHECK(fixture.resume_calls == 1 && fixture.callbacks_unlocked);
    gs232b_fake_spi_bus_destroy(&fixture.fake);
    return true;
}

static bool test_running_reset_preserves_directions_and_stop_wins(void)
{
    fixture_t fixture;
    CHECK(fixture_init(&fixture));
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
    CHECK(complete_reset(&fixture));
    CHECK(gs232b_output_manager_set_pwm(&fixture.outputs, 630) == GS232B_OK);
    CHECK(gs232b_output_manager_set_directions(&fixture.outputs,
          GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    CHECK(fixture.fake.latched == 0x15 && fixture.fake.pwm == 630);
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
    CHECK(fixture.fake.latched == 0x05 && fixture.fake.pwm == 630);
    CHECK(query(&fixture) == GS232B_ERROR_NOT_READY);
    advance(&fixture, GS232B_W5500_RESET_DEFAULT_US);
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    CHECK(fixture.fake.latched == 0x15 && fixture.fake.pwm == 630);
    CHECK(gs232b_output_manager_stop_all(&fixture.outputs) == GS232B_OK);
    CHECK(fixture.fake.latched == 0x10 && fixture.fake.pwm == 0);
    advance(&fixture, GS232B_W5500_RECOVERY_DEFAULT_US);
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    CHECK(fixture.reset.phase == GS232B_W5500_READY && fixture.fake.latched == 0x10);
    CHECK(gs232b_output_manager_set_directions(&fixture.outputs,
          GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
    CHECK(gs232b_output_manager_stop_all(&fixture.outputs) == GS232B_OK);
    CHECK(fixture.fake.latched == 0);
    CHECK(complete_reset(&fixture));
    CHECK(fixture.fake.latched == 0x10 && fixture.fake.pwm == 0);
    CHECK(fixture.outputs.state.requested.azimuth == GS232B_AZ_STOP);
    CHECK(fixture.outputs.state.requested.elevation == GS232B_EL_STOP);
    gs232b_fake_spi_bus_destroy(&fixture.fake);
    return true;
}

static bool test_pending_reversal_is_not_enabled_by_reset(void)
{
    fixture_t fixture;
    CHECK(fixture_init(&fixture));
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
    CHECK(complete_reset(&fixture));
    CHECK(gs232b_output_manager_set_pwm(&fixture.outputs, 650) == GS232B_OK);
    CHECK(gs232b_output_manager_set_directions(&fixture.outputs,
          GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    CHECK(gs232b_output_manager_set_directions(&fixture.outputs,
          GS232B_AZ_CCW, GS232B_EL_UP) == GS232B_OK);
    CHECK(fixture.fake.latched == 0x11 && fixture.fake.pwm == 0);
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
    CHECK(fixture.fake.latched == 0x01);
    CHECK(complete_reset(&fixture));
    CHECK(fixture.fake.latched == 0x11 && fixture.fake.pwm == 0);
    CHECK(fixture.outputs.state.reversal_waiting[GS232B_AXIS_AZIMUTH]);
    CHECK(gs232b_output_manager_tick(&fixture.outputs) == GS232B_OK);
    CHECK(fixture.fake.latched == 0x19 && fixture.fake.pwm == 650);
    gs232b_fake_spi_bus_destroy(&fixture.fake);
    return true;
}

static bool test_async_lifecycle_and_failures(void)
{
    fixture_t fixture;
    CHECK(fixture_init(&fixture));
    fixture.quiesce_error = GS232B_ERROR_WOULD_BLOCK;
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
    CHECK(fixture.reset.phase == GS232B_W5500_QUIESCING && fixture.fake.latched == 0);
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    CHECK(fixture.quiesce_calls == 2);
    fixture.quiesce_error = GS232B_OK;
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    CHECK(fixture.reset.phase == GS232B_W5500_RESET_LOW);
    advance(&fixture, GS232B_W5500_RESET_DEFAULT_US);
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    fixture.resume_error = GS232B_ERROR_WOULD_BLOCK;
    advance(&fixture, GS232B_W5500_RECOVERY_DEFAULT_US);
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    CHECK(fixture.reset.phase == GS232B_W5500_RESUMING);
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_ERROR_BUSY);
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    CHECK(fixture.resume_calls == 2);
    fixture.resume_error = GS232B_ERROR_IO;
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_ERROR_IO);
    CHECK(fixture.reset.phase == GS232B_W5500_FAILED && fixture.reset.last_error ==
          GS232B_ERROR_IO);
    CHECK(query(&fixture) == GS232B_ERROR_NOT_READY);
    const size_t count = fixture.fake.count;
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_ERROR_NOT_READY);
    CHECK(fixture.fake.count == count);
    fixture.resume_error = GS232B_OK;
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
    CHECK(complete_reset(&fixture));

    fixture.quiesce_error = GS232B_ERROR_TIMEOUT;
    gs232b_fake_spi_bus_clear(&fixture.fake);
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_ERROR_TIMEOUT);
    CHECK(fixture.fake.count == 1 && fixture.fake.trace[0].kind == SPI_QUIESCE);
    CHECK(query(&fixture) == GS232B_ERROR_NOT_READY);
    CHECK(fixture.fake.latched == 0x10 && fixture.outputs.state.output.known);
    CHECK(fixture.callbacks_unlocked && atomic_load(&fixture.fake.safe));
    gs232b_fake_spi_bus_destroy(&fixture.fake);
    return true;
}

static bool test_output_faults_stay_unknown_until_explicit_stop(void)
{
    for (unsigned at_release = 0; at_release < 2; ++at_release) {
        fixture_t fixture;
        CHECK(fixture_init(&fixture));
        CHECK(gs232b_output_manager_set_w5500_reset(&fixture.outputs, false) == GS232B_OK);
        if (at_release != 0) {
            CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
            advance(&fixture, GS232B_W5500_RESET_DEFAULT_US);
        }
        gs232b_fake_spi_bus_fault(&fixture.fake, SPI_SHIFT_BYTE, 0, GS232B_ERROR_TIMEOUT);
        CHECK((at_release ? gs232b_w5500_reset_tick(&fixture.reset) :
               gs232b_w5500_reset_begin(&fixture.reset)) == GS232B_ERROR_TIMEOUT);
        CHECK(fixture.reset.phase == GS232B_W5500_FAILED);
        CHECK(!fixture.outputs.state.output.known && fixture.outputs.state.output.last_error ==
              GS232B_ERROR_TIMEOUT);
        CHECK(query(&fixture) == GS232B_ERROR_NOT_READY && fixture.resume_calls == 0);
        const size_t count = fixture.fake.count;
        CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_ERROR_NOT_READY);
        CHECK(fixture.fake.count == count && !fixture.outputs.state.output.known);
        CHECK(gs232b_output_manager_stop_all(&fixture.outputs) == GS232B_OK);
        CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
        CHECK(complete_reset(&fixture));
        CHECK(fixture.fake.latched == 0x10 && fixture.fake.pwm == 0);
        gs232b_fake_spi_bus_destroy(&fixture.fake);
    }
    return true;
}

static bool test_fault_during_wait_does_not_get_overwritten(void)
{
    fixture_t fixture;
    CHECK(fixture_init(&fixture));
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
    gs232b_fake_spi_bus_fault(&fixture.fake, SPI_COMPLETE, 0, GS232B_ERROR_IO);
    CHECK(gs232b_output_manager_stop_all(&fixture.outputs) == GS232B_ERROR_IO);
    advance(&fixture, GS232B_W5500_RESET_DEFAULT_US);
    const size_t count = fixture.fake.count;
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_ERROR_NOT_READY);
    CHECK(fixture.fake.count == count && !fixture.outputs.state.output.known);
    CHECK(fixture.outputs.state.output.last_error == GS232B_ERROR_IO);
    CHECK(fixture.reset.phase == GS232B_W5500_FAILED && fixture.resume_calls == 0);
    gs232b_fake_spi_bus_destroy(&fixture.fake);
    return true;
}

static bool test_gate_failure_and_invalid_configuration(void)
{
    fixture_t fixture;
    CHECK(fixture_init(&fixture));
    const gs232b_w5500_lifecycle_t lifecycle = {&fixture, quiesce, resume};
    CHECK(gs232b_w5500_reset_init(&fixture.reset, &fixture.outputs, &fixture.bus,
          lifecycle, GS232B_W5500_RESET_MIN_US - 1U, GS232B_W5500_RECOVERY_MIN_US) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_w5500_reset_init(&fixture.reset, &fixture.outputs, &fixture.bus,
          lifecycle, GS232B_W5500_RESET_MIN_US, GS232B_W5500_RECOVERY_MIN_US - 1U) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(fixture.reset.phase == GS232B_W5500_OFFLINE && fixture.fake.count == 0);
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
    advance(&fixture, GS232B_W5500_RESET_DEFAULT_US);
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_OK);
    advance(&fixture, GS232B_W5500_RECOVERY_DEFAULT_US);
    fixture.fake.lock_error = GS232B_ERROR_TIMEOUT;
    CHECK(gs232b_w5500_reset_tick(&fixture.reset) == GS232B_ERROR_TIMEOUT);
    CHECK(fixture.resume_calls == 0 && fixture.reset.phase == GS232B_W5500_FAILED);
    CHECK(fixture.fake.latched == 0x10 && query(&fixture) == GS232B_ERROR_NOT_READY);
    CHECK(gs232b_w5500_reset_begin(&fixture.reset) == GS232B_OK);
    CHECK(complete_reset(&fixture));
    gs232b_fake_spi_bus_destroy(&fixture.fake);
    return true;
}

int main(void)
{
    if (!test_boot_and_completion_relative_timing() ||
        !test_running_reset_preserves_directions_and_stop_wins() ||
        !test_pending_reversal_is_not_enabled_by_reset() ||
        !test_async_lifecycle_and_failures() ||
        !test_output_faults_stay_unknown_until_explicit_stop() ||
        !test_fault_during_wait_does_not_get_overwritten() ||
        !test_gate_failure_and_invalid_configuration()) { return 1; }
    puts("external W5500 reset timing, lifecycle and fault tests passed");
    return 0;
}
