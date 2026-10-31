#include "sequence_test_support.h"

static bool test_first_point_and_t_acceptance(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_EMPTY, 0, 0, 0));
    CHECK(sequence_query(&fixture, 1, "=0000=0000\r\n"));
    CHECK(command_feed(&fixture, 0, "T\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(sequence_response(&fixture, 0, "?>\r\n"));
    CHECK(command_unowned(&fixture));

    CHECK(command_feed(&fixture, 0, "M001 150 140 100\r") == GS232B_OK);
    CHECK(fixture.targets_accepted == 0);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.targets_accepted == 1 && fixture.target_at_us == 0);
    CHECK(fixture.last_target.targets_mdeg[0] == 150000);
    CHECK(fixture.sim.motion.state.axes[0].target_valid);
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_LOADED_WAIT_T, 1, 3, 0));
    CHECK(sequence_query(&fixture, 1, "=0001=0003\r\n"));
    CHECK(command_feed(&fixture, 0, "T\r") == GS232B_OK);
    CHECK(sequence_at(&fixture, 123456));
    CHECK(sequence_response(&fixture, 0, "\r"));
    CHECK(fixture.targets_accepted == 1);
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_RUNNING, 1, 3, 1123456));
    CHECK(sequence_at(&fixture, 1123455));
    CHECK(fixture.targets_accepted == 1);
    CHECK(sequence_at(&fixture, 1123456));
    CHECK(fixture.targets_accepted == 2 && fixture.last_target.targets_mdeg[0] == 140000);
    CHECK(sequence_query(&fixture, 2, "=0002=0003\r\n"));
    return true;
}

static bool test_all_intervals_and_repeated_t(void)
{
    command_fixture_t fixture;
    for (unsigned seconds = 1; seconds <= 999; ++seconds) {
        CHECK(command_fixture_init(&fixture, 17));
        char line[40];
        (void)snprintf(line, sizeof(line), "M%03u 090 090 090\r", seconds);
        CHECK(command_run(&fixture, 0, line));
        CHECK(sequence_at(&fixture, 17 + 2 * (gs232b_time_us_t)seconds * SECOND_US));
        CHECK(sequence_state(&fixture, GS232B_SEQUENCE_LOADED_WAIT_T, 1, 3, 0));
        CHECK(fixture.targets_accepted == 1);
        CHECK(command_owner(&fixture, 0));
        const gs232b_time_us_t start = fixture.sim.now_us;
        const gs232b_time_us_t step = seconds * SECOND_US;
        CHECK(command_run(&fixture, 0, "T\r"));
        CHECK(sequence_at(&fixture, start + step - 1));
        CHECK(command_run(&fixture, 0, "T\r"));
        CHECK(sequence_response(&fixture, 0, "\r"));
        CHECK(sequence_state(&fixture, GS232B_SEQUENCE_RUNNING, 1, 3, start + step));
        CHECK(fixture.targets_accepted == 1);
        CHECK(sequence_at(&fixture, start + step));
        CHECK(sequence_state(&fixture, GS232B_SEQUENCE_RUNNING, 2, 3, start + 2 * step));
        CHECK(fixture.targets_accepted == 2);
        CHECK(command_run(&fixture, 0, "T\r"));
        CHECK(sequence_at(&fixture, start + 2 * step - 1));
        CHECK(fixture.targets_accepted == 2);
        CHECK(sequence_at(&fixture, start + 2 * step));
        CHECK(sequence_state(&fixture, GS232B_SEQUENCE_COMPLETED, 3, 3, 0));
        CHECK(fixture.targets_accepted == 3 && fixture.sequence_targets == 2);
        CHECK(sequence_query(&fixture, 1, "=0003=0003\r\n"));
    }
    return true;
}

static bool test_absolute_deadlines_with_jitter(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "M010 090 090 090 090\r"));
    CHECK(command_run(&fixture, 0, "T\r"));
    CHECK(sequence_at(&fixture, 19999999));
    CHECK(fixture.targets_accepted == 2);
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_RUNNING, 2, 4, 20000000));
    CHECK(sequence_at(&fixture, 19999999));
    CHECK(fixture.targets_accepted == 2);
    CHECK(sequence_at(&fixture, 20000000));
    CHECK(fixture.targets_accepted == 3);
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_RUNNING, 3, 4, 30000000));
    CHECK(sequence_at(&fixture, 30000321));
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_COMPLETED, 4, 4, 0));
    CHECK(fixture.target_at_us == 30000321 && fixture.targets_accepted == 4);
    return true;
}

static bool test_completion_before_mechanical_arrival_and_idle_release(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "W001 090 090 100 110\r"));
    CHECK(command_run(&fixture, 0, "T\r"));
    CHECK(sequence_at(&fixture, SECOND_US));
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_COMPLETED, 2, 2, 0));
    CHECK(fixture.sim.motion.state.axes[0].target_valid);
    CHECK(fixture.sim.motion.state.axes[1].target_valid);
    CHECK(fixture.last_target.targets_mdeg[0] == 100000);
    CHECK(fixture.last_target.targets_mdeg[1] == 110000);
    CHECK(sequence_query(&fixture, 2, "=0002=0002\r\n"));
    CHECK(command_feed(&fixture, 0, "T\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(sequence_response(&fixture, 0, "?>\r\n"));
    CHECK(fixture.targets_accepted == 2);
    CHECK(command_advance(&fixture, 700));
    CHECK(stopped(&fixture.sim));
    CHECK(command_unowned(&fixture));
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_COMPLETED, 2, 2, 0));
    CHECK(sequence_query(&fixture, 1, "=0002=0002\r\n"));
    CHECK(gs232b_control_take(&fixture.control, command_context(&fixture, 1)) == GS232B_OK);
    CHECK(gs232b_control_release(&fixture.control, command_context(&fixture, 1)) == GS232B_OK);
    CHECK(sequence_query(&fixture, 2, "=0002=0002\r\n"));
    CHECK(command_run(&fixture, 0, "M001 090 090\r"));
    CHECK(sequence_query(&fixture, 1, "=0001=0002\r\n"));
    CHECK(command_run(&fixture, 0, "T\r"));
    return true;
}

static bool test_maximum_sequence_execution(void)
{
    command_fixture_t fixture;
    for (size_t paired = 0; paired < 2; ++paired) {
        const uint16_t count = paired ? 1900 : 3800;
        CHECK(command_fixture_init(&fixture, 0));
        CHECK(command_feed(&fixture, 0, paired ? "W999" : "M999") == GS232B_OK);
        for (uint16_t point = 0; point < count; ++point) {
            CHECK(command_feed(&fixture, 0, paired ? " 090 090" : " 090") == GS232B_OK);
        }
        CHECK(command_run(&fixture, 0, "\r"));
        CHECK(command_run(&fixture, 0, "T\r"));
        for (uint16_t point = 1; point < count; ++point) {
            CHECK(sequence_at(&fixture, (gs232b_time_us_t)point * 999 * SECOND_US));
            CHECK(fixture.targets_accepted == (size_t)point + 1);
        }
        CHECK(sequence_state(&fixture, GS232B_SEQUENCE_COMPLETED, count, count, 0));
        CHECK(sequence_query(&fixture, 1, paired ? "=1900=1900\r\n" : "=3800=3800\r\n"));
    }
    return true;
}

int main(void)
{
    if (!test_first_point_and_t_acceptance() || !test_all_intervals_and_repeated_t() ||
        !test_absolute_deadlines_with_jitter() ||
        !test_completion_before_mechanical_arrival_and_idle_release() ||
        !test_maximum_sequence_execution()) {
        return EXIT_FAILURE;
    }
    puts("sequence first point, 1..999 second intervals, T/N and absolute timing passed");
    return EXIT_SUCCESS;
}
