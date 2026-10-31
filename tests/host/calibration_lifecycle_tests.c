#include "calibration_test_support.h"

static bool test_timeout_does_not_refresh_on_bad_input_or_frames(void)
{
    static const char *const starts[] = {"O\r", "O2\r", "F\r", "F2\r"};
    command_fixture_t fixture;
    for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        CHECK(command_fixture_init(&fixture, 0));
        CHECK(command_run(&fixture, 0, starts[i]));
        fixture.sim.now_us = GS232B_CALIBRATION_TIMEOUT_US - 1000;
        CHECK(command_feed(&fixture, 0, "R\r") == GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(command_run(&fixture, 1, "C\r"));
        CHECK(fixture.control.calibration.kind != GS232B_INTERACTION_NONE);
        const size_t frames = fixture.async_responses[0];
        CHECK(command_feed(&fixture, 0, "Y") == GS232B_OK);
        fixture.sim.now_us = GS232B_CALIBRATION_TIMEOUT_US;
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_TIMEOUT);
        CHECK(calibration_response(&fixture, 0, "?>\r\n"));
        CHECK(fixture.async_responses[0] == frames + 1);
        CHECK(fixture.async_responses[1] == 0 && fixture.async_responses[2] == 0);
        CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
        CHECK(fixture.control.sessions[0].parser.discarding);
        CHECK(command_feed(&fixture, 0, "ES\r") == GS232B_OK);
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(fixture.async_responses[0] == frames + 1);
        CHECK(calibration_no_records(&fixture));
        CHECK(stopped(&fixture.sim));
        CHECK(command_run(&fixture, 0, "C\r"));
        fixture.sim.now_us += GS232B_CONTROL_IDLE_RELEASE_US;
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(command_unowned(&fixture));
    }
    return true;
}

static bool test_cancel_invalidates_partial_without_duplicate_error(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "O\r"));
    CHECK(command_feed(&fixture, 0, "NO\rR") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    const size_t replies = fixture.completes[0];
    CHECK(fixture.control.sessions[0].parser.discarding);
    CHECK(command_feed(&fixture, 0, "\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.completes[0] == replies && fixture.executed == 0);
    CHECK(command_run(&fixture, 0, "R\r"));
    CHECK(fixture.executed == 1);
    return true;
}

static bool test_confirmation_at_timeout_cannot_save(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "O\r"));
    fixture.sim.now_us = GS232B_CALIBRATION_TIMEOUT_US;
    CHECK(command_feed(&fixture, 0, "YES\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_TIMEOUT);
    CHECK(calibration_no_records(&fixture));
    CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
    CHECK(fixture.async_responses[0] == 1);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.async_responses[0] == 1);
    return true;
}

static bool test_disconnect_and_generation_reuse(void)
{
    static const char *const starts[] = {"O\r", "O2\r", "F\r", "F2\r"};
    command_fixture_t fixture;
    for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        CHECK(command_fixture_init(&fixture, 0));
        CHECK(command_run(&fixture, 1, starts[i]));
        CHECK(gs232b_control_session_close(&fixture.control, fixture.sessions[2]) == GS232B_OK);
        CHECK(fixture.control.calibration.kind != GS232B_INTERACTION_NONE);
        CHECK(command_feed(&fixture, 1, "YES\r") ==
            (i < 2 ? GS232B_OK : GS232B_ERROR_INVALID_ARGUMENT));
        const gs232b_command_context_t old = command_context(&fixture, 1);
        const size_t replies = fixture.completes[1];
        CHECK(gs232b_control_session_close(&fixture.control, fixture.sessions[1]) == GS232B_OK);
        CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
        CHECK(command_unowned(&fixture));
        CHECK(fixture.completes[1] == replies);
        CHECK(fixture.async_responses[0] == 0 && fixture.async_responses[2] == 0);
        CHECK(gs232b_control_session_open(&fixture.control, GS232B_TRANSPORT_ETHERNET_WS,
            fixture.line_storage[1], sizeof(fixture.line_storage[1]),
            &fixture.sessions[1]) == GS232B_OK);
        CHECK(fixture.sessions[1].session_id == old.session.session_id);
        CHECK(fixture.sessions[1].generation > old.session.generation);
        static const uint8_t yes[] = "YES\r";
        CHECK(gs232b_control_feed(&fixture.control, old, yes, sizeof(yes) - 1) ==
              GS232B_ERROR_STALE_SESSION);
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(calibration_no_records(&fixture));
        CHECK(stopped(&fixture.sim));
    }
    return true;
}

static bool test_save_failures_preserve_ram_and_persisted_zero(void)
{
    command_fixture_t fixture;
    static const gs232b_fake_store_operation_t failures[] = {
        GS232B_FAKE_STORE_LOAD, GS232B_FAKE_STORE_SAVE, GS232B_FAKE_STORE_INTERRUPT
    };
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
            CHECK(command_fixture_init(&fixture, 0));
            CHECK(calibration_seed(&fixture, GS232B_AXIS_AZIMUTH));
            CHECK(calibration_seed(&fixture, GS232B_AXIS_ELEVATION));
            CHECK(command_run(&fixture, 0, axis == 0 ? "O\r" : "O2\r"));
            const gs232b_store_domain_t domain = axis == 0 ?
                GS232B_STORE_ZERO_AZ : GS232B_STORE_ZERO_EL;
            const gs232b_fake_store_record_t old = fixture.store.records[domain];
            CHECK(gs232b_fake_fail_next(&fixture.store.failures[domain][failures[i]],
                                        GS232B_ERROR_IO, 1) == GS232B_OK);
            fixture.sim.adc_override[axis] = true;
            fixture.sim.adc_uv[axis] = 200000;
            CHECK(command_feed(&fixture, 0, "YES\r") == GS232B_OK);
            CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_IO);
            CHECK(calibration_response(&fixture, 0, "?>\r\n"));
            CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
            CHECK(calibration_stored(&fixture, (gs232b_axis_id_t)axis, 100000));
            CHECK(calibration_stored(&fixture, (gs232b_axis_id_t)(1 - axis), 100000));
            CHECK(fixture.store.records[domain].size == old.size);
            CHECK(memcmp(fixture.store.records[domain].bytes, old.bytes, old.size) == 0);
        }
    }
    return true;
}

static bool test_invalid_confirmation_sample_and_span(void)
{
    command_fixture_t fixture;
    for (int failure = 0; failure < 6; ++failure) {
        CHECK(command_fixture_init(&fixture, 0));
        CHECK(calibration_seed(&fixture, GS232B_AXIS_AZIMUTH));
        CHECK(command_run(&fixture, 0, "O\r"));
        fixture.sim.now_us += GS232B_FEEDBACK_SAMPLE_MAX_AGE_US + 1;
        fixture.sim.sample_invalid[0] = failure == 0;
        fixture.sim.sample_error[0] = failure == 1 ? GS232B_ERROR_IO : GS232B_OK;
        fixture.sim.sample_frozen[0] = failure == 2 || failure == 5;
        fixture.sim.sample_at_us[0] = failure == 5 ? fixture.sim.now_us + 1 : 0;
        fixture.sim.adc_override[0] = failure == 3 || failure == 4;
        fixture.sim.adc_uv[0] = failure == 3 ? GS232B_FEEDBACK_FULL_SCALE_UV :
                                              GS232B_FEEDBACK_FULL_SCALE_UV + 1;
        CHECK(command_feed(&fixture, 0, "Y\r") == GS232B_OK);
        CHECK(gs232b_control_tick(&fixture.control) != GS232B_OK);
        CHECK(calibration_response(&fixture, 0, "?>\r\n"));
        CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
        CHECK(calibration_stored(&fixture, GS232B_AXIS_AZIMUTH, 100000));
    }
    return true;
}

static bool test_display_failure_and_transport_cleanup(void)
{
    command_fixture_t fixture;
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        CHECK(command_fixture_init(&fixture, 0));
        CHECK(command_run(&fixture, 1, "F2\r"));
        fixture.sim.now_us += 1;
        fixture.sim.sample_invalid[axis] = true;
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_NOT_READY);
        CHECK(calibration_response(&fixture, 1, "?>\r\n"));
        CHECK(fixture.async_responses[1] == 1);
        CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(fixture.async_responses[1] == 1);
        CHECK(calibration_no_records(&fixture));
    }
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "F\r"));
    fixture.respond_error = GS232B_ERROR_IO;
    fixture.sim.now_us += GS232B_CALIBRATION_DISPLAY_PERIOD_US;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_IO);
    CHECK(!fixture.control.sessions[0].connected);
    CHECK(command_unowned(&fixture));
    CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
    CHECK(stopped(&fixture.sim));
    CHECK(fixture.async_responses[1] == 0 && fixture.async_responses[2] == 0);
    return true;
}

static bool test_clock_rollback_and_near_overflow(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, UINT64_MAX - GS232B_CALIBRATION_TIMEOUT_US));
    CHECK(command_run(&fixture, 0, "F\r"));
    fixture.sim.now_us = UINT64_MAX - 1;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.async_responses[0] == 1);
    fixture.sim.now_us = UINT64_MAX;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_TIMEOUT);
    CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
    CHECK(fixture.async_responses[0] == 2);
    CHECK(command_fixture_init(&fixture, 1000));
    CHECK(command_run(&fixture, 0, "O\r"));
    fixture.sim.now_us = 999;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_TIMEOUT);
    CHECK(calibration_state(&fixture, GS232B_INTERACTION_NONE, 0));
    CHECK(calibration_no_records(&fixture));
    return true;
}

int main(void)
{
    if (!test_timeout_does_not_refresh_on_bad_input_or_frames() ||
        !test_cancel_invalidates_partial_without_duplicate_error() ||
        !test_confirmation_at_timeout_cannot_save() ||
        !test_disconnect_and_generation_reuse() ||
        !test_save_failures_preserve_ram_and_persisted_zero() ||
        !test_invalid_confirmation_sample_and_span() ||
        !test_clock_rollback_and_near_overflow() ||
        !test_display_failure_and_transport_cleanup()) {
        return EXIT_FAILURE;
    }
    puts("calibration timeout, disconnect, sampling and persistence failure tests passed");
    return EXIT_SUCCESS;
}
