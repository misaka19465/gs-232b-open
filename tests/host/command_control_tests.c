#include "command_test_support.h"

static bool test_owner_and_nonowner_permissions(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "C\r"));
    CHECK(command_run(&fixture, 1, "!OWNER GET\r"));
    CHECK(command_unowned(&fixture));
    CHECK(command_run(&fixture, 0, "R\r"));
    CHECK(command_owner(&fixture, 0));
    CHECK(command_advance(&fixture, 15));
    CHECK(fixture.sim.pwm_permille != 0);

    static const char *const queries[] = {
        "C\r", "B\r", "C2\r", "N\r", "H\r", "H2\r", "H3\r", "!MOTION STATUS\r"
    };
    for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); ++i) {
        CHECK(command_run(&fixture, 1 + i % 2, queries[i]));
    }
    static const char *const writes[] = {
        "R\r", "L\r", "U\r", "D\r", "M180\r", "W180 120\r", "X1\r", "T\r",
        "O\r", "O2\r", "F\r", "F2\r", "P36\r", "P45\r", "Z\r", "YES\r"
    };
    const size_t executed = fixture.executed;
    for (size_t i = 0; i < sizeof(writes) / sizeof(writes[0]); ++i) {
        CHECK(command_feed(&fixture, 1 + i % 2, writes[i]) == GS232B_ERROR_NOT_OWNER);
    }
    CHECK(fixture.executed == executed);
    CHECK(fixture.sim.motion.config.azimuth_speed_permille == 800);
    CHECK(fixture.sim.motion.axes[0].manual_direction == GS232B_MOTION_POSITIVE);
    CHECK(gs232b_control_take(&fixture.control, command_context(&fixture, 1)) ==
          GS232B_ERROR_NOT_OWNER);
    CHECK(gs232b_control_release(&fixture.control, command_context(&fixture, 1)) ==
          GS232B_ERROR_NOT_OWNER);
    CHECK(gs232b_control_release(&fixture.control, command_context(&fixture, 0)) ==
          GS232B_ERROR_BUSY);

    CHECK(command_run(&fixture, 2, "S\r"));
    CHECK(command_unowned(&fixture));
    CHECK(stopped(&fixture.sim));
    CHECK(command_run(&fixture, 1, "!OwNeR   tAkE\r"));
    CHECK(command_owner(&fixture, 1));
    CHECK(command_run(&fixture, 1, "!OWNER RELEASE\r"));
    CHECK(command_unowned(&fixture));
    return true;
}

static bool test_idle_release_and_holds(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(gs232b_control_take(&fixture.control, command_context(&fixture, 0)) == GS232B_OK);
    const gs232b_command_epoch_t epoch = fixture.control.epoch;
    fixture.sim.now_us = GS232B_CONTROL_IDLE_RELEASE_US - 1;
    CHECK(command_run(&fixture, 0, "C\r"));
    CHECK(command_run(&fixture, 1, "N\r"));
    CHECK(command_owner(&fixture, 0));
    ++fixture.sim.now_us;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(command_unowned(&fixture));
    CHECK(fixture.control.epoch > epoch);

    CHECK(command_run(&fixture, 0, "R\r"));
    CHECK(command_advance(&fixture, 110));
    CHECK(command_owner(&fixture, 0));
    CHECK(command_run(&fixture, 1, "A\r"));
    CHECK(command_owner(&fixture, 0));
    CHECK(command_advance(&fixture, 99));
    CHECK(command_owner(&fixture, 0));
    CHECK(command_advance(&fixture, 1));
    CHECK(command_unowned(&fixture));

    CHECK(command_run(&fixture, 0, "X1\r"));
    for (int state = GS232B_SEQUENCE_LOADED_WAIT_T;
         state <= GS232B_SEQUENCE_RUNNING; ++state) {
        fixture.sequence = (gs232b_sequence_state_t)state;
        fixture.sim.now_us += UINT64_C(999000000);
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(command_owner(&fixture, 0));
        CHECK(gs232b_control_release(&fixture.control, command_context(&fixture, 0)) ==
              GS232B_ERROR_BUSY);
    }
    fixture.sequence = GS232B_SEQUENCE_EMPTY;
    for (int interaction = GS232B_INTERACTION_ZERO_AZ;
         interaction <= GS232B_INTERACTION_DISPLAY_BOTH; ++interaction) {
        fixture.interaction = (gs232b_interaction_t)interaction;
        fixture.sim.now_us += UINT64_C(3000000);
        CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
        CHECK(command_owner(&fixture, 0));
    }
    fixture.interaction = GS232B_INTERACTION_NONE;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    fixture.sim.now_us += GS232B_CONTROL_IDLE_RELEASE_US;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(command_unowned(&fixture));
    return true;
}

static bool test_invalid_writes_and_take_race(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_feed(&fixture, 0, "M999\r") == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(command_feed(&fixture, 0, "Rubbish\r") == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(command_feed(&fixture, 0, "!OWNER TAKE extra\r") == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(command_feed(&fixture, 0, "!NET SET {}\r") != GS232B_OK);
    CHECK(command_unowned(&fixture));
    CHECK(command_feed(&fixture, 0, "R\r") == GS232B_OK);
    CHECK(command_feed(&fixture, 1, "U\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_NOT_OWNER);
    CHECK(command_owner(&fixture, 0));
    CHECK(fixture.executed == 1);
    CHECK(fixture.last_error[1] == GS232B_ERROR_NOT_OWNER);
    CHECK(command_run(&fixture, 2, "S\r"));

    fixture.execute_error = GS232B_ERROR_NOT_READY;
    CHECK(command_feed(&fixture, 0, "R\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_NOT_READY);
    CHECK(command_unowned(&fixture));
    fixture.execute_error = GS232B_OK;
    CHECK(gs232b_control_session_close(&fixture.control, fixture.sessions[2]) == GS232B_OK);
    CHECK(gs232b_control_session_open(&fixture.control, GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS,
        fixture.line_storage[2], sizeof(fixture.line_storage[2]), &fixture.sessions[2]) == GS232B_OK);
    CHECK(command_feed(&fixture, 2, "R\r") == GS232B_ERROR_NOT_AUTHORIZED);
    CHECK(gs232b_control_take(&fixture.control, command_context(&fixture, 2)) ==
          GS232B_ERROR_NOT_AUTHORIZED);
    CHECK(command_run(&fixture, 2, "C\r"));
    CHECK(command_run(&fixture, 2, "S\r"));
    return true;
}

static bool test_bounded_queue_and_safety_cadence(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    for (size_t session = 0; session < CONTROL_TEST_SESSIONS; ++session) {
        for (size_t i = 0; i < GS232B_CONTROL_REQUESTS_PER_SESSION; ++i) {
            CHECK(command_feed(&fixture, session, "C\r") == GS232B_OK);
        }
        CHECK(command_feed(&fixture, session, "C\r") == GS232B_ERROR_RESOURCE_EXHAUSTED);
    }
    CHECK(fixture.control.request_count == 24);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.executed == 1 && fixture.ticks == 1);
    CHECK(command_feed(&fixture, 0, "C\r") == GS232B_OK);
    CHECK(command_feed(&fixture, 1, "S\rS\r") == GS232B_OK);
    const size_t replies = fixture.completes[1];
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.executed == 1 && fixture.ticks == 2);
    CHECK(fixture.control.request_count == 0 && fixture.control.request_bytes_used == 0);
    CHECK(fixture.completes[1] == replies + 2);
    CHECK(fixture.last_access[1] == GS232B_REQUEST_STOP_ALL);
    CHECK(stopped(&fixture.sim));
    return true;
}

static bool test_queued_interaction_input_and_isolation(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_feed(&fixture, 0, "O\rN\r") == GS232B_OK);
    CHECK(command_feed(&fixture, 1, "N\r\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.interaction == GS232B_INTERACTION_ZERO_AZ);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.interaction == GS232B_INTERACTION_NONE);
    CHECK(fixture.last_access[0] == GS232B_REQUEST_WRITE);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(command_run(&fixture, 0, "F\r"));
    CHECK(fixture.interaction == GS232B_INTERACTION_DISPLAY_AZ);
    CHECK(command_run(&fixture, 1, "\r"));
    CHECK(fixture.interaction == GS232B_INTERACTION_DISPLAY_AZ);
    CHECK(command_run(&fixture, 1, "N\r"));
    CHECK(fixture.interaction == GS232B_INTERACTION_DISPLAY_AZ);
    CHECK(command_run(&fixture, 1, "S\r"));
    CHECK(fixture.interaction == GS232B_INTERACTION_NONE);
    CHECK(command_unowned(&fixture));
    return true;
}

int main(void)
{
    if (!test_owner_and_nonowner_permissions() || !test_idle_release_and_holds() ||
        !test_invalid_writes_and_take_race() || !test_bounded_queue_and_safety_cadence() ||
        !test_queued_interaction_input_and_isolation()) {
        return EXIT_FAILURE;
    }
    puts("command ownership, permissions, idle release and queue tests passed");
    return EXIT_SUCCESS;
}
