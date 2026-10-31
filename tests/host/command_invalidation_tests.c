#include "command_test_support.h"

static bool test_stop_discards_queue_partial_and_old_rx(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "R\r"));
    CHECK(command_advance(&fixture, 15));
    CHECK(command_feed(&fixture, 0, "L\rU\rX1\rM010 150 ") == GS232B_OK);
    CHECK(command_feed(&fixture, 2, "R") == GS232B_OK);
    const gs232b_command_context_t old_rx = command_context(&fixture, 0);
    const size_t executed = fixture.executed;
    CHECK(command_run(&fixture, 1, "s\r"));
    CHECK(command_unowned(&fixture));
    CHECK(fixture.control.epoch > old_rx.epoch);
    CHECK(fixture.control.request_count == 0);
    CHECK(stopped(&fixture.sim));
    CHECK(command_feed(&fixture, 0, "140\r") == GS232B_OK);
    CHECK(command_feed(&fixture, 2, "\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.executed == executed);
    CHECK(stopped(&fixture.sim));

    static const uint8_t old_commands[] = "R\rU\rL";
    CHECK(gs232b_control_feed(&fixture.control, old_rx, old_commands,
                              sizeof(old_commands) - 1) == GS232B_ERROR_CONFLICT);
    CHECK(command_feed(&fixture, 0, "\r") == GS232B_OK);
    CHECK(command_advance(&fixture, 120));
    CHECK(fixture.executed == executed);
    CHECK(stopped(&fixture.sim));
    static const uint8_t old_complete[] = "R\r\nU\r\n";
    CHECK(gs232b_control_feed(&fixture.control, old_rx, old_complete,
                              sizeof(old_complete) - 1) == GS232B_ERROR_CONFLICT);
    CHECK(!fixture.control.sessions[0].parser.active);
    CHECK(command_run(&fixture, 0, "C\r"));
    CHECK(fixture.executed == executed + 1);
    CHECK(command_run(&fixture, 2, "U\r"));
    CHECK(command_owner(&fixture, 2));
    return true;
}

static bool test_stale_queued_contexts_are_discarded_at_execution(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "S\r"));
    CHECK(command_feed(&fixture, 0, "R\rC\r") == GS232B_OK);
    --fixture.control.requests[0].context.epoch;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_CONFLICT);
    CHECK(fixture.executed == 0 && fixture.control.request_count == 1);
    CHECK(command_unowned(&fixture));
    CHECK(stopped(&fixture.sim));
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.queries == 1 && fixture.control.request_count == 0);
    CHECK(command_feed(&fixture, 0, "R\r") == GS232B_OK);
    ++fixture.control.requests[0].context.session.generation;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_STALE_SESSION);
    CHECK(fixture.executed == 1 && fixture.control.request_count == 0);
    CHECK(command_unowned(&fixture));
    CHECK(stopped(&fixture.sim));
    return true;
}

static bool test_axis_stops_cancel_sequence_and_preserve_other_axis(void)
{
    command_fixture_t fixture;
    for (int stopped_axis = 0; stopped_axis < GS232B_AXIS_COUNT; ++stopped_axis) {
        CHECK(command_fixture_init(&fixture, 0));
        CHECK(command_run(&fixture, 0, "R\r"));
        CHECK(command_run(&fixture, 0, "U\r"));
        CHECK(command_advance(&fixture, 15));
        fixture.sequence = GS232B_SEQUENCE_RUNNING;
        fixture.sequence_would_restart = true;
        fixture.interaction = GS232B_INTERACTION_ZERO_AZ;
        CHECK(command_feed(&fixture, 0, "T\rR\rU\r") == GS232B_OK);
        const gs232b_command_epoch_t epoch = fixture.control.epoch;
        CHECK(command_run(&fixture, 1, stopped_axis == 0 ? "A\r" : "E\r"));
        CHECK(command_owner(&fixture, 0));
        CHECK(fixture.sequence == GS232B_SEQUENCE_EMPTY);
        CHECK(fixture.interaction == GS232B_INTERACTION_NONE);
        CHECK(!fixture.sequence_would_restart);
        CHECK(fixture.control.epoch > epoch);
        CHECK(fixture.control.request_count == 0);
        CHECK(command_advance(&fixture, 20));
        gs232b_motion_snapshot_t state;
        CHECK(snapshot(&fixture.sim, &state));
        CHECK(state.axes[stopped_axis].mode == GS232B_AXIS_IDLE);
        CHECK(!state.axes[stopped_axis].target_valid);
        CHECK(state.axes[1 - stopped_axis].mode == GS232B_AXIS_MANUAL);
        CHECK(stopped_axis == 0 ? state.output.last_successful.azimuth == GS232B_AZ_STOP :
                                 state.output.last_successful.elevation == GS232B_EL_STOP);
        CHECK(command_run(&fixture, 2, "S\r"));
        CHECK(stopped(&fixture.sim));
    }
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "R\r"));
    CHECK(command_run(&fixture, 0, "U\r"));
    CHECK(command_feed(&fixture, 1, "A\r") == GS232B_OK);
    CHECK(command_feed(&fixture, 2, "E\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(command_owner(&fixture, 0));
    CHECK(stopped(&fixture.sim));
    return true;
}

static bool test_close_and_generation_reuse(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "R\r"));
    CHECK(command_feed(&fixture, 0, "U\rL") == GS232B_OK);
    const gs232b_command_context_t old = command_context(&fixture, 0);
    CHECK(gs232b_control_session_close(&fixture.control, fixture.sessions[1]) == GS232B_OK);
    CHECK(command_owner(&fixture, 0));
    CHECK(gs232b_control_session_close(&fixture.control, fixture.sessions[0]) == GS232B_OK);
    CHECK(command_unowned(&fixture));
    CHECK(stopped(&fixture.sim));
    CHECK(gs232b_control_session_open(&fixture.control, GS232B_TRANSPORT_UART1,
        fixture.line_storage[0], sizeof(fixture.line_storage[0]), &fixture.sessions[0]) == GS232B_OK);
    CHECK(fixture.sessions[0].session_id == old.session.session_id);
    CHECK(fixture.sessions[0].generation > old.session.generation);
    CHECK(command_unowned(&fixture));
    CHECK(command_run(&fixture, 0, "C\r"));
    CHECK(command_unowned(&fixture));
    CHECK(gs232b_control_take(&fixture.control, old) == GS232B_ERROR_STALE_SESSION);
    CHECK(gs232b_control_release(&fixture.control, old) == GS232B_ERROR_STALE_SESSION);
    CHECK(gs232b_control_request_stop(&fixture.control, old, GS232B_REQUEST_STOP_ALL) ==
          GS232B_ERROR_STALE_SESSION);
    CHECK(gs232b_control_session_close(&fixture.control, old.session) == GS232B_ERROR_STALE_SESSION);
    static const uint8_t move[] = "R\r";
    CHECK(gs232b_control_feed(&fixture.control, old, move, sizeof(move) - 1) ==
          GS232B_ERROR_STALE_SESSION);
    CHECK(command_run(&fixture, 2, "U\r"));
    CHECK(command_feed(&fixture, 0, "R\r") == GS232B_ERROR_NOT_OWNER);
    CHECK(command_owner(&fixture, 2));
    CHECK(command_run(&fixture, 0, "S\r"));
    CHECK(stopped(&fixture.sim));

    CHECK(command_feed(&fixture, 0, "R\r") == GS232B_OK);
    CHECK(command_feed(&fixture, 2, "S\r") == GS232B_OK);
    CHECK(gs232b_control_session_close(&fixture.control, fixture.sessions[2]) == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(command_unowned(&fixture));
    CHECK(stopped(&fixture.sim));
    return true;
}

static bool test_stop_only_complete_independent_lines(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "R\r"));
    CHECK(command_advance(&fixture, 15));
    CHECK(command_feed(&fixture, 1, "!UNKNOWN {\"ssid\":\"S\"}\r") != GS232B_OK);
    CHECK(command_feed(&fixture, 1, "M010 150 S\r") == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(command_feed(&fixture, 1, "S\n\r") == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(command_feed(&fixture, 1, "AS\r") == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(command_feed(&fixture, 1, "S") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.control.stop_mask == 0);
    CHECK(fixture.sim.pwm_permille != 0);
    CHECK(command_run(&fixture, 1, "\r\n"));
    CHECK(stopped(&fixture.sim));
    CHECK(command_unowned(&fixture));
    return true;
}

static bool test_byte_pool_exhaustion_and_epoch_overflow(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "R\r"));
    uint8_t block[GS232B_CONTROL_FEED_MAX_BYTES];
    memset(block, 'x', sizeof(block));
    for (size_t line = 0; line < 3; ++line) {
        CHECK(command_feed(&fixture, 1, "!UNKNOWN ") == GS232B_OK);
        for (size_t i = 0; i < 7; ++i) {
            CHECK(gs232b_control_feed(&fixture.control, command_context(&fixture, 1),
                                      block, sizeof(block)) == GS232B_OK);
        }
        CHECK(command_feed(&fixture, 1, "\r") != GS232B_OK);
    }
    /* 长序列仅持有 staging；第二条不能覆盖等待执行的第一条。 */
    for (size_t line = 0; line < 2; ++line) {
        CHECK(command_feed(&fixture, 0, "M010") == GS232B_OK);
        for (size_t i = 0; i < 3800; ++i) {
            CHECK(command_feed(&fixture, 0, " 150") == GS232B_OK);
        }
        const gs232b_error_t expected = line == 0 ? GS232B_OK : GS232B_ERROR_BUSY;
        CHECK(command_feed(&fixture, 0, "\r") == expected);
    }
    CHECK(fixture.control.request_count == 1);
    CHECK(fixture.control.request_bytes_used == 0);
    CHECK(command_run(&fixture, 1, "S\r"));
    CHECK(stopped(&fixture.sim));
    CHECK(fixture.control.request_bytes_used == 0);

    fixture.control.epoch = UINT64_MAX;
    CHECK(command_run(&fixture, 0, "R\r"));
    CHECK(command_run(&fixture, 1, "S\r"));
    CHECK(fixture.control.epoch == UINT64_MAX && fixture.control.epoch_exhausted);
    CHECK(command_unowned(&fixture));
    CHECK(stopped(&fixture.sim));
    CHECK(command_feed(&fixture, 0, "R\r") == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(gs232b_control_take(&fixture.control, command_context(&fixture, 0)) ==
          GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(command_run(&fixture, 1, "C\r"));
    CHECK(command_run(&fixture, 1, "S\r"));
    return true;
}

static bool test_fault_revokes_old_work_and_failed_stop_stays_unknown(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "R\r"));
    CHECK(command_feed(&fixture, 0, "L\rU\r") == GS232B_OK);
    fixture.sequence = GS232B_SEQUENCE_RUNNING;
    fixture.interaction = GS232B_INTERACTION_ZERO_AZ;
    const gs232b_command_epoch_t epoch = fixture.control.epoch;
    fixture.sim.sample_invalid[0] = true;
    CHECK(gs232b_control_tick(&fixture.control) != GS232B_OK);
    CHECK(command_unowned(&fixture));
    CHECK(fixture.control.epoch > epoch && fixture.control.request_count == 0);
    CHECK(fixture.sequence == GS232B_SEQUENCE_EMPTY);
    CHECK(fixture.interaction == GS232B_INTERACTION_NONE);
    CHECK(stopped(&fixture.sim));
    fixture.sim.sample_invalid[0] = false;
    CHECK(gs232b_motion_clear_fault(&fixture.sim.motion) == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(stopped(&fixture.sim));
    CHECK(command_run(&fixture, 1, "R\r"));
    fixture.sim.output_failures = 2;
    CHECK(command_feed(&fixture, 0, "S\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_IO);
    CHECK(command_unowned(&fixture));
    CHECK(fixture.control.request_count == 0);
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(&fixture.sim, &state));
    CHECK(!state.output.known && state.fault.code == GS232B_FAULT_OUTPUT);
    const size_t attempts = fixture.sim.output_attempts;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_IO);
    CHECK(fixture.sim.output_attempts == attempts);

    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "R\r"));
    fixture.snapshot_error = GS232B_ERROR_IO;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_IO);
    CHECK(fixture.control.owner.session_id == 0);
    CHECK(stopped(&fixture.sim));
    fixture.snapshot_error = GS232B_OK;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(command_unowned(&fixture));
    return true;
}

int main(void)
{
    if (!test_stop_discards_queue_partial_and_old_rx() ||
        !test_stale_queued_contexts_are_discarded_at_execution() ||
        !test_axis_stops_cancel_sequence_and_preserve_other_axis() ||
        !test_close_and_generation_reuse() || !test_stop_only_complete_independent_lines() ||
        !test_byte_pool_exhaustion_and_epoch_overflow() ||
        !test_fault_revokes_old_work_and_failed_stop_stays_unknown()) {
        return EXIT_FAILURE;
    }
    puts("command stop priority, epochs, partial lines and session reuse tests passed");
    return EXIT_SUCCESS;
}
