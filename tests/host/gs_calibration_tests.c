#include "gs_test_support.h"

static bool test_zero_confirmations_use_source_and_fresh_sample(void)
{
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        gs_fixture_t fixture;
        CHECK(gs_init(&fixture, true));
        fixture.sim.adc_override[axis] = true;
        fixture.sim.adc_uv[axis] = 150000;
        CHECK(gs_run(&fixture, 0, axis == 0 ? "o\r" : "o2\r", axis == 0 ?
            "AZoffset\r\nare you sure? yes or no\r\n" :
            "ELoffset\r\nare you sure? yes or no\r\n"));
        CHECK(gs_feed(&fixture, 1, "Y\r") == GS232B_ERROR_NOT_OWNER);
        CHECK(gs_expect(&fixture, 1, "?>\r\n"));
        CHECK(gs_run(&fixture, 2, "N\r", "=0000=0000\r\n"));
        CHECK(gs_run(&fixture, 2, "\r", "\r"));
        CHECK(fixture.dispatcher.control.calibration.kind != GS232B_INTERACTION_NONE);
        fixture.sim.adc_uv[axis] = 200000;
        CHECK(gs_run(&fixture, 0, "yEs\r", "Completed\r\n"));
        uint32_t zero_uv;
        const gs232b_store_t store = gs232b_fake_store_port(&fixture.store);
        CHECK(gs232b_feedback_zero_load(&store, (gs232b_axis_id_t)axis, &zero_uv) == GS232B_OK);
        CHECK(zero_uv == 200000);
        CHECK(fixture.sim.motion.config.axes[axis].zero_uv == 200000);
        CHECK(fixture.sim.motion.config.axes[1 - axis].zero_uv == 100000);
        CHECK(!fixture.store.records[GS232B_STORE_DISPLAY].present);
        CHECK(fixture.dispatcher.control.calibration.kind == GS232B_INTERACTION_NONE);
        CHECK(gs_feed(&fixture, 0, "YES\r") == GS232B_OK);
        CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(gs_expect(&fixture, 0, "?>\r\n"));
        CHECK(stopped(&fixture.sim));
        CHECK(gs_quiet(&fixture));
    }
    return true;
}

static bool test_queued_confirmation_cancels_and_failed_save(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_feed(&fixture, 0, "O\rY\r") == GS232B_OK);
    CHECK(gs_drain(&fixture));
    CHECK(gs_expect(&fixture, 0, "AZoffset\r\nare you sure? yes or no\r\nCompleted\r\n"));
    static const char *const cancel[] = {"n\r", "no\r", "\r"};
    for (size_t i = 0; i < sizeof(cancel) / sizeof(cancel[0]); ++i) {
        CHECK(gs_run(&fixture, 0, "O2\r", "ELoffset\r\nare you sure? yes or no\r\n"));
        CHECK(gs_run(&fixture, 0, cancel[i], "\r"));
        CHECK(!fixture.store.records[GS232B_STORE_ZERO_EL].present);
    }
    CHECK(gs_run(&fixture, 0, "O2\r", "ELoffset\r\nare you sure? yes or no\r\n"));
    CHECK(gs232b_fake_fail_next(&fixture.store.failures[GS232B_STORE_ZERO_EL]
        [GS232B_FAKE_STORE_INTERRUPT], GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs_feed(&fixture, 0, "Y\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_ERROR_IO);
    CHECK(gs_expect(&fixture, 0, "?>\r\n"));
    CHECK(fixture.sim.motion.config.axes[1].zero_uv == 100000);
    CHECK(!fixture.store.records[GS232B_STORE_ZERO_EL].present);
    CHECK(fixture.dispatcher.control.calibration.kind == GS232B_INTERACTION_NONE);
    CHECK(gs_quiet(&fixture));
    return true;
}

static bool test_calibration_display_period_exit_and_no_writes(void)
{
    for (size_t both = 0; both < 2; ++both) {
        gs_fixture_t fixture;
        CHECK(gs_init(&fixture, true));
        const char *const frame = both ? "AZ=090  EL=090\r\n" : "AZ=090\r\n";
        CHECK(gs_run(&fixture, 0, both ? "f2\r" : "f\r", frame));
        fixture.sim.now_us = UINT64_C(499999);
        CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
        CHECK(gs_quiet(&fixture));
        fixture.sim.now_us = UINT64_C(500000);
        CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
        CHECK(gs_expect(&fixture, 0, frame));
        CHECK(gs_run(&fixture, 1, "C2\r", "AZ=090  EL=090\r\n"));
        CHECK(gs_feed(&fixture, 0, "M180\r") != GS232B_OK);
        CHECK(gs_expect(&fixture, 0, "?>\r\n"));
        CHECK(stopped(&fixture.sim));
        fixture.sim.now_us = 3 * GS_SECOND_US;
        CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
        CHECK(gs_expect(&fixture, 0, frame));
        CHECK(gs_run(&fixture, 0, "\r\n", "\r"));
        fixture.sim.now_us += GS_SECOND_US;
        CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
        CHECK(gs_quiet(&fixture));
        CHECK(stopped(&fixture.sim));
        for (int domain = 0; domain < GS232B_STORE_DOMAIN_COUNT; ++domain) {
            CHECK(!fixture.store.records[domain].present);
        }
    }
    return true;
}

static bool test_stops_cancel_interaction_only_reply_to_sender(void)
{
    static const char *const stop[] = {"A\r", "E\r", "S\r"};
    for (size_t i = 0; i < 3; ++i) {
        gs_fixture_t fixture;
        CHECK(gs_init(&fixture, true));
        CHECK(gs_run(&fixture, 0, "F2\r", "AZ=090  EL=090\r\n"));
        CHECK(gs_run(&fixture, 1, stop[i], "\r"));
        CHECK(fixture.dispatcher.control.calibration.kind == GS232B_INTERACTION_NONE);
        CHECK(gs_quiet(&fixture));
        CHECK(stopped(&fixture.sim));
    }
    return true;
}

static bool test_expiry_and_invalid_feedback_do_not_leak_frames(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_run(&fixture, 0, "O\r", "AZoffset\r\nare you sure? yes or no\r\n"));
    fixture.sim.now_us = 59 * GS_SECOND_US;
    CHECK(gs_feed(&fixture, 0, "Y") == GS232B_OK);
    fixture.sim.now_us = 60 * GS_SECOND_US;
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_ERROR_TIMEOUT);
    CHECK(gs_expect(&fixture, 0, "?>\r\n"));
    CHECK(gs_feed(&fixture, 0, "ES\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
    CHECK(gs_quiet(&fixture));
    CHECK(!fixture.store.records[GS232B_STORE_ZERO_AZ].present);
    CHECK(gs_run(&fixture, 0, "F2\r", "AZ=090  EL=090\r\n"));
    fixture.sim.sample_invalid[1] = true;
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) != GS232B_OK);
    CHECK(gs_expect(&fixture, 0, "?>\r\n"));
    CHECK(fixture.dispatcher.control.calibration.kind == GS232B_INTERACTION_NONE);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
    CHECK(gs_quiet(&fixture));
    return true;
}

int main(void)
{
    if (!test_zero_confirmations_use_source_and_fresh_sample() ||
        !test_queued_confirmation_cancels_and_failed_save() ||
        !test_calibration_display_period_exit_and_no_writes() ||
        !test_stops_cancel_interaction_only_reply_to_sender() ||
        !test_expiry_and_invalid_feedback_do_not_leak_frames()) {
        return EXIT_FAILURE;
    }
    puts("GS O/O2 confirmations, F/F2 display and calibration response isolation passed");
    return EXIT_SUCCESS;
}
