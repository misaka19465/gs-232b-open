#include "gs232b/output_manager.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

#define WAIT_US UINT64_C(100)
#define TRACE_CAPACITY 64U

typedef struct {
    gs232b_time_us_t started_at_us;
    gs232b_time_us_t completed_at_us;
    uint8_t shadow;
    uint16_t pwm;
    gs232b_error_t result;
} event_t;

typedef struct {
    gs232b_output_manager_t manager;
    gs232b_time_us_t now_us;
    gs232b_time_us_t latency_us;
    gs232b_error_t next_error;
    event_t trace[TRACE_CAPACITY];
    size_t count;
    bool safe;
    uint8_t previous_direction[GS232B_AXIS_COUNT];
    bool stopped[GS232B_AXIS_COUNT];
    gs232b_time_us_t stopped_at_us[GS232B_AXIS_COUNT];
} fixture_t;

static gs232b_time_us_t now_us(void *ctx)
{
    return ((fixture_t *)ctx)->now_us;
}

static gs232b_error_t driver_apply(void *ctx, uint8_t shadow, uint16_t pwm)
{
    fixture_t *fixture = ctx;
    if (fixture->count >= TRACE_CAPACITY) {
        fixture->safe = false;
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    const gs232b_time_us_t started_at_us = fixture->now_us;
    fixture->now_us += fixture->latency_us;
    const gs232b_error_t result = fixture->next_error;
    fixture->next_error = GS232B_OK;
    fixture->trace[fixture->count++] = (event_t){
        .started_at_us = started_at_us,
        .completed_at_us = fixture->now_us,
        .shadow = shadow,
        .pwm = pwm,
        .result = result
    };

    if ((shadow & 0xe0U) != 0 || (shadow & 0x03U) == 0x03U ||
        (shadow & 0x0cU) == 0x0cU || pwm > 1000U ||
        ((shadow & 0x0cU) == 0 && pwm != 0)) {
        fixture->safe = false;
    }
    const uint8_t directions[] = {(uint8_t)((shadow >> 2) & 3U),
                                  (uint8_t)(shadow & 3U)};
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        if (result != GS232B_OK) {
            fixture->previous_direction[i] = 0;
            fixture->stopped[i] = false;
        } else if (directions[i] == 0) {
            if (!fixture->stopped[i]) {
                fixture->stopped_at_us[i] = fixture->now_us;
                fixture->stopped[i] = true;
            }
        } else {
            if (directions[i] != fixture->previous_direction[i] &&
                (!fixture->stopped[i] || started_at_us < fixture->stopped_at_us[i] ||
                 started_at_us - fixture->stopped_at_us[i] < WAIT_US)) {
                fixture->safe = false;
            }
            fixture->previous_direction[i] = directions[i];
            fixture->stopped[i] = false;
        }
    }
    return result;
}

static bool fixture_init(fixture_t *fixture, gs232b_time_us_t start_us,
                         gs232b_time_us_t latency_us, gs232b_error_t initial_error)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->safe = true;
    fixture->now_us = start_us;
    fixture->latency_us = latency_us;
    fixture->next_error = initial_error;
    CHECK(gs232b_output_manager_init(
        &fixture->manager, (gs232b_clock_t){fixture, now_us},
        (gs232b_output_driver_t){fixture, driver_apply}, WAIT_US) == initial_error);
    CHECK(fixture->count == 1);
    CHECK(fixture->trace[0].shadow == 0 && fixture->trace[0].pwm == 0);
    CHECK(fixture->safe);
    return true;
}

static const event_t *last_event(const fixture_t *fixture)
{
    return &fixture->trace[fixture->count - 1U];
}

static bool read_snapshot(fixture_t *fixture, gs232b_output_manager_snapshot_t *snapshot)
{
    CHECK(gs232b_output_manager_snapshot(&fixture->manager, snapshot) == GS232B_OK);
    CHECK(snapshot->shadow_byte == last_event(fixture)->shadow);
    CHECK(fixture->safe);
    return true;
}

static bool frame_equal(gs232b_output_frame_t left, gs232b_output_frame_t right)
{
    return left.azimuth == right.azimuth && left.elevation == right.elevation &&
           left.speed_permille == right.speed_permille &&
           left.w5500_reset_asserted == right.w5500_reset_asserted;
}

static bool test_all_direction_and_reset_transitions(void)
{
    static const uint8_t az_bits[] = {0x00, 0x04, 0x08};
    static const uint8_t el_bits[] = {0x00, 0x01, 0x02};
    for (int source = 0; source < 18; ++source) {
        for (int target = 0; target < 18; ++target) {
            fixture_t fixture;
            CHECK(fixture_init(&fixture, 0, 0, GS232B_OK));
            fixture.now_us += WAIT_US;
            gs232b_output_frame_t from = {
                .azimuth = (gs232b_az_direction_t)((source / 3) % 3),
                .elevation = (gs232b_el_direction_t)(source % 3),
                .speed_permille = 500,
                .w5500_reset_asserted = source < 9
            };
            const gs232b_output_t port = gs232b_output_manager_port(&fixture.manager);
            CHECK(gs232b_output_apply(&port, &from) == GS232B_OK);
            const uint8_t source_shadow = az_bits[from.azimuth] | el_bits[from.elevation] |
                                           (from.w5500_reset_asserted ? 0 : 0x10);
            CHECK(last_event(&fixture)->shadow == source_shadow);

            gs232b_output_frame_t to = {
                .azimuth = (gs232b_az_direction_t)((target / 3) % 3),
                .elevation = (gs232b_el_direction_t)(target % 3),
                .speed_permille = 750,
                .w5500_reset_asserted = target < 9
            };
            const bool az_reverse = from.azimuth != GS232B_AZ_STOP &&
                                    to.azimuth != GS232B_AZ_STOP &&
                                    from.azimuth != to.azimuth;
            const bool el_reverse = from.elevation != GS232B_EL_STOP &&
                                    to.elevation != GS232B_EL_STOP &&
                                    from.elevation != to.elevation;
            CHECK(gs232b_output_apply(&port, &to) == GS232B_OK);
            const uint8_t reset = to.w5500_reset_asserted ? 0 : 0x10;
            const uint8_t immediate_shadow = (az_reverse ? 0 : az_bits[to.azimuth]) |
                                              (el_reverse ? 0 : el_bits[to.elevation]) |
                                              reset;
            CHECK(last_event(&fixture)->shadow == immediate_shadow);
            gs232b_output_manager_snapshot_t snapshot;
            CHECK(read_snapshot(&fixture, &snapshot));
            CHECK(frame_equal(snapshot.requested, to));
            CHECK(snapshot.reversal_waiting[GS232B_AXIS_AZIMUTH] == az_reverse);
            CHECK(snapshot.reversal_waiting[GS232B_AXIS_ELEVATION] == el_reverse);

            const size_t count = fixture.count;
            fixture.now_us += WAIT_US - 1U;
            CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
            CHECK(fixture.count == count);
            ++fixture.now_us;
            CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
            CHECK(last_event(&fixture)->shadow ==
                  (uint8_t)(az_bits[to.azimuth] | el_bits[to.elevation] | reset));
            CHECK(last_event(&fixture)->pwm == (to.azimuth == GS232B_AZ_STOP ? 0 : 750));
            CHECK(read_snapshot(&fixture, &snapshot));
            CHECK(!snapshot.reversal_waiting[0] && !snapshot.reversal_waiting[1]);

            const uint8_t directions = (uint8_t)(snapshot.shadow_byte & 0x0fU);
            CHECK(gs232b_output_manager_set_w5500_reset(&fixture.manager,
                                                        !to.w5500_reset_asserted) ==
                  GS232B_OK);
            CHECK((last_event(&fixture)->shadow & 0x0fU) == directions);
            CHECK((last_event(&fixture)->shadow & 0x10U) == (uint8_t)(reset ^ 0x10U));
            CHECK(gs232b_output_manager_set_pwm(&fixture.manager, 0) == GS232B_OK);
            CHECK((last_event(&fixture)->shadow & 0x0fU) == directions);
            CHECK(last_event(&fixture)->pwm == 0);
            CHECK(gs232b_output_manager_set_pwm(&fixture.manager, 1000) == GS232B_OK);
            CHECK((last_event(&fixture)->shadow & 0x0fU) == directions);
            CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                        GS232B_AZ_STOP, to.elevation) ==
                  GS232B_OK);
            CHECK((last_event(&fixture)->shadow & 0x0cU) == 0);
            CHECK((last_event(&fixture)->shadow & 0x03U) == el_bits[to.elevation]);
            CHECK((last_event(&fixture)->shadow & 0x10U) == (uint8_t)(reset ^ 0x10U));
            CHECK(gs232b_output_manager_stop_all(&fixture.manager) == GS232B_OK);
            CHECK(last_event(&fixture)->shadow == (uint8_t)(reset ^ 0x10U));
            CHECK(last_event(&fixture)->pwm == 0 && fixture.safe);
        }
    }
    return true;
}

static bool test_wait_starts_after_completed_stop(void)
{
    fixture_t fixture;
    CHECK(fixture_init(&fixture, 0, 37, GS232B_OK));
    CHECK(gs232b_output_manager_set_pwm(&fixture.manager, 650) == GS232B_OK);
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    CHECK(fixture.count == 1);
    fixture.now_us = 37 + WAIT_US - 1U;
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(fixture.count == 1);
    ++fixture.now_us;
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x05 && last_event(&fixture)->pwm == 650);

    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CCW, GS232B_EL_UP) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x01 && last_event(&fixture)->pwm == 0);
    const gs232b_time_us_t stopped_at_us = last_event(&fixture)->completed_at_us;
    fixture.now_us = stopped_at_us + WAIT_US / 2U;
    const size_t count = fixture.count;
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CCW, GS232B_EL_UP) == GS232B_OK);
    CHECK(fixture.count == count);
    fixture.now_us = stopped_at_us + WAIT_US - 1U;
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(fixture.count == count);

    CHECK(gs232b_output_manager_set_w5500_reset(&fixture.manager, false) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x11);
    CHECK(fixture.now_us > stopped_at_us + WAIT_US);
    CHECK(gs232b_output_manager_set_pwm(&fixture.manager, 900) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x11 && last_event(&fixture)->pwm == 0);
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x19 && last_event(&fixture)->pwm == 900);
    CHECK(fixture.safe);
    return true;
}

static bool test_stop_then_reverse_and_cancellation(void)
{
    fixture_t fixture;
    CHECK(fixture_init(&fixture, 1000, 0, GS232B_OK));
    fixture.now_us += WAIT_US;
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    CHECK(gs232b_output_manager_stop_all(&fixture.manager) == GS232B_OK);
    const gs232b_time_us_t stopped_at_us = fixture.now_us;
    fixture.now_us += 5;
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CCW, GS232B_EL_DOWN) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0);
    fixture.now_us = stopped_at_us + WAIT_US - 1U;
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0);
    ++fixture.now_us;
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x0a);

    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0);
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_STOP, GS232B_EL_STOP) == GS232B_OK);
    fixture.now_us += WAIT_US * 2U;
    const size_t count = fixture.count;
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(fixture.count == count);
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CCW, GS232B_EL_DOWN) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x0a);
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0);
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CCW, GS232B_EL_DOWN) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x0a);
    CHECK(fixture.safe);
    return true;
}

static bool test_axes_wait_independently(void)
{
    fixture_t fixture;
    CHECK(fixture_init(&fixture, 0, 0, GS232B_OK));
    fixture.now_us += WAIT_US;
    CHECK(gs232b_output_manager_set_pwm(&fixture.manager, 700) == GS232B_OK);
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CCW, GS232B_EL_UP) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x01);
    fixture.now_us += WAIT_US / 2U;
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CCW, GS232B_EL_DOWN) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0);
    fixture.now_us += WAIT_US / 2U;
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x08 && last_event(&fixture)->pwm == 700);
    CHECK(gs232b_output_manager_set_w5500_reset(&fixture.manager, false) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x18);
    fixture.now_us += WAIT_US / 2U;
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x1a);
    CHECK(fixture.safe);
    return true;
}

static bool test_failures_cancel_motion_and_require_recovery(void)
{
    for (int failure_site = 0; failure_site < 5; ++failure_site) {
        fixture_t fixture;
        CHECK(fixture_init(&fixture, 0, 0, GS232B_OK));
        fixture.now_us += WAIT_US;
        CHECK(gs232b_output_manager_set_pwm(&fixture.manager, 500) == GS232B_OK);
        CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                    GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
        if (failure_site == 3) {
            CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                      GS232B_AZ_CCW, GS232B_EL_DOWN) ==
                  GS232B_OK);
            fixture.now_us += WAIT_US;
        }
        gs232b_output_manager_snapshot_t before, failed, recovered;
        CHECK(read_snapshot(&fixture, &before));
        fixture.next_error = GS232B_ERROR_TIMEOUT;
        gs232b_error_t result;
        switch (failure_site) {
        case 0: result = gs232b_output_manager_set_pwm(&fixture.manager, 750); break;
        case 1: result = gs232b_output_manager_set_w5500_reset(&fixture.manager, false); break;
        case 2: result = gs232b_output_manager_set_directions(
                    &fixture.manager, GS232B_AZ_CCW, GS232B_EL_DOWN); break;
        case 3: result = gs232b_output_manager_tick(&fixture.manager); break;
        default: result = gs232b_output_manager_stop_all(&fixture.manager); break;
        }
        CHECK(result == GS232B_ERROR_TIMEOUT);
        CHECK(read_snapshot(&fixture, &failed));
        CHECK(!failed.output.known && failed.output.last_error == GS232B_ERROR_TIMEOUT);
        CHECK(frame_equal(failed.output.last_successful, before.output.last_successful));
        CHECK(failed.requested.azimuth == GS232B_AZ_STOP);
        CHECK(failed.requested.elevation == GS232B_EL_STOP);
        CHECK(failed.requested.speed_permille == 0);
        CHECK(!failed.reversal_waiting[0] && !failed.reversal_waiting[1]);
        const size_t count = fixture.count;
        fixture.now_us += WAIT_US * 10U;
        CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_ERROR_NOT_READY);
        CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                    GS232B_AZ_CCW, GS232B_EL_UP) ==
              GS232B_ERROR_NOT_READY);
        CHECK(fixture.count == count);
        CHECK(read_snapshot(&fixture, &recovered));
        CHECK(frame_equal(recovered.requested, failed.requested));

        fixture.next_error = GS232B_ERROR_IO;
        CHECK(gs232b_output_manager_stop_all(&fixture.manager) == GS232B_ERROR_IO);
        CHECK(read_snapshot(&fixture, &recovered));
        CHECK(!recovered.output.known);
        CHECK(frame_equal(recovered.output.last_successful, before.output.last_successful));
        CHECK((last_event(&fixture)->shadow & 0x0fU) == 0);
        CHECK(gs232b_output_manager_stop_all(&fixture.manager) == GS232B_OK);
        CHECK(read_snapshot(&fixture, &recovered));
        CHECK(recovered.output.known && recovered.output.last_error == GS232B_OK);
        CHECK(recovered.shadow_byte == (failed.requested.w5500_reset_asserted ? 0 : 0x10));
        CHECK(last_event(&fixture)->pwm == 0);

        CHECK(gs232b_output_manager_set_pwm(&fixture.manager, 600) == GS232B_OK);
        CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                    GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
        CHECK((last_event(&fixture)->shadow & 0x0fU) == 0);
        fixture.now_us += WAIT_US - 1U;
        CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
        CHECK((last_event(&fixture)->shadow & 0x0fU) == 0);
        ++fixture.now_us;
        CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
        CHECK((last_event(&fixture)->shadow & 0x0fU) == 0x05);
        CHECK(last_event(&fixture)->pwm == 600 && fixture.safe);
    }
    return true;
}

static bool test_startup_failure_and_updates_after_unknown(void)
{
    fixture_t fixture;
    CHECK(fixture_init(&fixture, 0, 0, GS232B_ERROR_IO));
    gs232b_output_manager_snapshot_t snapshot;
    CHECK(read_snapshot(&fixture, &snapshot));
    CHECK(!snapshot.output.known);
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CW, GS232B_EL_STOP) ==
          GS232B_ERROR_NOT_READY);
    CHECK(gs232b_output_manager_stop_all(&fixture.manager) == GS232B_OK);
    fixture.now_us += WAIT_US;
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    fixture.next_error = GS232B_ERROR_IO;
    CHECK(gs232b_output_manager_set_pwm(&fixture.manager, 300) == GS232B_ERROR_IO);
    CHECK(gs232b_output_manager_set_w5500_reset(&fixture.manager, false) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x10 && last_event(&fixture)->pwm == 0);
    CHECK(read_snapshot(&fixture, &snapshot));
    CHECK(snapshot.output.known);
    CHECK(gs232b_output_manager_set_pwm(&fixture.manager, 1000) == GS232B_OK);
    fixture.now_us += WAIT_US * 2U;
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0x10 && last_event(&fixture)->pwm == 0);
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CCW, GS232B_EL_UP) == GS232B_OK);
    fixture.next_error = GS232B_ERROR_TIMEOUT;
    CHECK(gs232b_output_manager_set_w5500_reset(&fixture.manager, true) ==
          GS232B_ERROR_TIMEOUT);
    CHECK(read_snapshot(&fixture, &snapshot));
    CHECK(!snapshot.output.known && !snapshot.reversal_waiting[0]);
    CHECK(gs232b_output_manager_set_pwm(&fixture.manager, 800) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0 && last_event(&fixture)->pwm == 0);
    fixture.now_us += WAIT_US * 2U;
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(last_event(&fixture)->shadow == 0);
    CHECK(fixture.safe);
    return true;
}

static bool test_time_boundary_and_invalid_requests(void)
{
    fixture_t fixture;
    CHECK(fixture_init(&fixture, UINT64_MAX - WAIT_US / 2U, 0, GS232B_OK));
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                                                GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    fixture.now_us = UINT64_MAX;
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(fixture.count == 1);
    fixture.now_us = 0;
    CHECK(gs232b_output_manager_tick(&fixture.manager) == GS232B_OK);
    CHECK(fixture.count == 1);

    const gs232b_output_manager_t before = fixture.manager;
    CHECK(gs232b_output_manager_set_pwm(&fixture.manager, 1001) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                (gs232b_az_direction_t)-1, GS232B_EL_UP) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_output_manager_set_directions(&fixture.manager,
                GS232B_AZ_CW, (gs232b_el_direction_t)3) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_output_manager_apply(&fixture.manager, NULL) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(memcmp(&before, &fixture.manager, sizeof(before)) == 0);
    CHECK(fixture.count == 1);
    CHECK(gs232b_output_manager_init(&fixture.manager,
              (gs232b_clock_t){&fixture, now_us},
              (gs232b_output_driver_t){&fixture, driver_apply}, 0) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(memcmp(&before, &fixture.manager, sizeof(before)) == 0);

    gs232b_output_manager_t empty = {0};
    CHECK(gs232b_output_manager_tick(&empty) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_output_manager_stop_all(NULL) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_output_manager_set_w5500_reset(NULL, true) == GS232B_ERROR_INVALID_ARGUMENT);
    gs232b_output_manager_snapshot_t snapshot;
    memset(&snapshot, 0xff, sizeof(snapshot));
    CHECK(gs232b_output_manager_snapshot(NULL, &snapshot) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(!snapshot.output.known && snapshot.shadow_byte == 0);
    CHECK(gs232b_output_manager_snapshot(&fixture.manager, NULL) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(fixture.safe);
    return true;
}

int main(void)
{
    if (!test_all_direction_and_reset_transitions() ||
        !test_wait_starts_after_completed_stop() ||
        !test_stop_then_reverse_and_cancellation() ||
        !test_axes_wait_independently() ||
        !test_failures_cancel_motion_and_require_recovery() ||
        !test_startup_failure_and_updates_after_unknown() ||
        !test_time_boundary_and_invalid_requests()) {
        return 1;
    }
    puts("output manager tests passed (324 direction/reset transitions)");
    return 0;
}
