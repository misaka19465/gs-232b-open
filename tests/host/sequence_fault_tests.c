#include "sequence_test_support.h"

static bool test_overdue_targets_fault_without_catchup(void)
{
    command_fixture_t fixture;
    static const char *const lines[] = {
        "M001 120 140 160\r", "W001 120 120 140 140 160 160\r", "M001 120 140\r"
    };
    for (size_t index = 0; index < 3; ++index) {
        CHECK(command_fixture_init(&fixture, 0));
        CHECK(command_run(&fixture, 0, lines[index]));
        CHECK(command_run(&fixture, 0, "T\r"));
        CHECK(command_feed(&fixture, 0, "R\rT\r") == GS232B_OK);
        CHECK(command_feed(&fixture, 0, "W001 090 ") == GS232B_OK);
        const gs232b_command_epoch_t epoch = fixture.control.epoch;
        const size_t outputs = fixture.sim.output_attempts;
        fixture.sim.now_us = index == 1 ? 10 * SECOND_US : 2 * SECOND_US;
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_TIMEOUT);
        CHECK(sequence_fault(&fixture, GS232B_FAULT_SEQUENCE_LATE, GS232B_ERROR_TIMEOUT));
        CHECK(stopped(&fixture.sim));
        CHECK(fixture.faults == 1 && fixture.targets_accepted == 1);
        CHECK(fixture.target_attempts == 1 && fixture.sequence_targets == 0);
        CHECK(fixture.control.epoch > epoch && fixture.control.request_count == 0);
        CHECK(!gs232b_session_key_valid(fixture.control.input_reservation.session));
        CHECK(fixture.sim.output_attempts == outputs + 1);
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_TIMEOUT);
        CHECK(fixture.sim.output_attempts == outputs + 1 && fixture.faults == 1);
        CHECK(command_feed(&fixture, 1, "N\r") == GS232B_OK);
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_TIMEOUT);
        CHECK(sequence_response(&fixture, 1, "=0000=0000\r\n"));
        CHECK(gs232b_motion_clear_fault(&fixture.sim.motion) == GS232B_OK);
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(sequence_at(&fixture, fixture.sim.now_us + 3 * SECOND_US));
        CHECK(fixture.targets_accepted == 1 && stopped(&fixture.sim));
        CHECK(command_feed(&fixture, 1, "T\r") == GS232B_OK);
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(command_run(&fixture, 1, "W001 090 090 100 100\r"));
        CHECK(command_run(&fixture, 1, "T\r"));
    }
    return true;
}

static bool test_stop_wins_over_lateness_and_tick_time_is_rechecked(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "W001 120 120 140 140 160 160\r"));
    CHECK(command_run(&fixture, 0, "T\r"));
    CHECK(command_feed(&fixture, 1, "S\r") == GS232B_OK);
    CHECK(sequence_at(&fixture, 5 * SECOND_US));
    CHECK(fixture.faults == 0 && fixture.targets_accepted == 1);
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_EMPTY, 0, 0, 0));
    CHECK(stopped(&fixture.sim));

    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "M001 090 090 090\r"));
    CHECK(command_run(&fixture, 0, "T\r"));
    fixture.tick_latency_us = SECOND_US;
    fixture.sim.now_us = SECOND_US;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_TIMEOUT);
    CHECK(sequence_fault(&fixture, GS232B_FAULT_SEQUENCE_LATE, GS232B_ERROR_TIMEOUT));
    CHECK(fixture.target_attempts == 1 && fixture.targets_accepted == 1);
    CHECK(stopped(&fixture.sim));
    return true;
}

static bool test_rejected_target_and_failed_output_latch_faults(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "W001 120 120 140 140 160 160\r"));
    CHECK(command_run(&fixture, 0, "T\r"));
    fixture.sequence_error = GS232B_ERROR_NOT_READY;
    fixture.sim.now_us = SECOND_US;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_NOT_READY);
    CHECK(sequence_fault(&fixture, GS232B_FAULT_SEQUENCE_TARGET, GS232B_ERROR_NOT_READY));
    CHECK(fixture.targets_accepted == 1 && fixture.target_attempts == 2);
    CHECK(stopped(&fixture.sim));

    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "M001 090 180 270\r"));
    CHECK(command_run(&fixture, 0, "T\r"));
    fixture.sim.output_failures = 2;
    fixture.sim.now_us = SECOND_US;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_IO);
    CHECK(sequence_fault(&fixture, GS232B_FAULT_OUTPUT, GS232B_ERROR_IO));
    CHECK(!fixture.sim.motion.state.output.known);
    CHECK(fixture.target_attempts == 2 && fixture.targets_accepted == 1);
    const size_t attempts = fixture.sim.output_attempts;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_IO);
    CHECK(fixture.sim.output_attempts == attempts && fixture.target_attempts == 2);
    return true;
}

static bool test_timestamp_overflow_and_clock_regression(void)
{
    command_fixture_t fixture;
    const gs232b_time_us_t start = UINT64_MAX - 2 * SECOND_US;
    CHECK(command_fixture_init(&fixture, start));
    CHECK(command_run(&fixture, 0, "M001 090 090 090 090\r"));
    CHECK(command_feed(&fixture, 0, "T\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_LOADED_WAIT_T, 1, 4, 0));
    CHECK(fixture.targets_accepted == 1 && fixture.faults == 0);
    CHECK(command_run(&fixture, 0, "M001 090 090 090\r"));
    CHECK(command_run(&fixture, 0, "T\r"));
    CHECK(sequence_at(&fixture, UINT64_MAX - SECOND_US));
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_RUNNING, 2, 3, UINT64_MAX));
    CHECK(sequence_at(&fixture, UINT64_MAX));
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_COMPLETED, 3, 3, 0));

    CHECK(command_fixture_init(&fixture, 50 * SECOND_US));
    CHECK(command_run(&fixture, 0, "M001 090 090 090\r"));
    CHECK(command_run(&fixture, 0, "T\r"));
    CHECK(sequence_at(&fixture, 50500000));
    fixture.sim.now_us = 50400000;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_TIMEOUT);
    CHECK(sequence_fault(&fixture, GS232B_FAULT_SEQUENCE_LATE, GS232B_ERROR_TIMEOUT));
    CHECK(fixture.targets_accepted == 1 && stopped(&fixture.sim));
    return true;
}

int main(void)
{
    if (!test_overdue_targets_fault_without_catchup() ||
        !test_stop_wins_over_lateness_and_tick_time_is_rechecked() ||
        !test_rejected_target_and_failed_output_latch_faults() ||
        !test_timestamp_overflow_and_clock_regression()) {
        return EXIT_FAILURE;
    }
    puts("sequence missed deadlines, atomic failures, overflow and clock faults passed");
    return EXIT_SUCCESS;
}
