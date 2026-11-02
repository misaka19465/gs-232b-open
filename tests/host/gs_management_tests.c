#include "management_test_support.h"

bool management_matrix_tests(void);

static bool ownership_status_and_watchdog(void)
{
    gs_fixture_t f; runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(management_command(&f, 1, "!SYS INFO\r", GS232B_OK, "\"network\":3"));
    CHECK(management_command(&f, 1, "!OWNER GET\r", GS232B_OK, "\"owner\":null"));
    CHECK(management_command(&f, 0, "  !owner   take {}\r", GS232B_OK, "\"transport\":\"uart1\""));
    CHECK(management_command(&f, 1, "!OWNER TAKE\r", GS232B_ERROR_NOT_OWNER, NULL));
    CHECK(management_command(&f, 1, "!OWNER RELEASE\r", GS232B_ERROR_NOT_OWNER, NULL));
    CHECK(management_command(&f, 1, "!SESSION PING\r", GS232B_ERROR_NOT_OWNER, NULL));
    CHECK(management_command(&f, 1, "!SESSION WATCHDOG {\"timeout_ms\":1000}\r", GS232B_ERROR_NOT_OWNER, NULL));
    CHECK(gs_run(&f, 0, "R\r", "\r"));
    CHECK(management_command(&f, 0, "!OWNER RELEASE\r", GS232B_ERROR_BUSY, NULL));
    CHECK(management_command(&f, 1, "!OWNER GET\r", GS232B_OK, "\"azimuth\""));
    CHECK(management_command(&f, 1, "!MOTION STATUS\r", GS232B_OK, "\"raw\":"));
    CHECK(management_command(&f, 0, "!SESSION WATCHDOG {\"timeout_ms\":1000}\r", GS232B_OK, "\"enabled\":true"));
    gs232b_time_us_t renewed = f.dispatcher.watchdog_renewed_us;
    f.sim.now_us += 300000;
    CHECK(management_command(&f, 1, "!MOTION STATUS\r", GS232B_OK, NULL));
    CHECK(management_command(&f, 1, "!SYS INFO\r", GS232B_OK, NULL));
    CHECK(management_command(&f, 1, "!OWNER GET\r", GS232B_OK, NULL));
    CHECK(gs_run(&f, 1, "C\r", "AZ=090\r\n"));
    CHECK(f.dispatcher.watchdog_renewed_us == renewed);
    CHECK(management_command(&f, 0, "!SESSION WATCHDOG {\"timeout_ms\":999}\r", GS232B_ERROR_INVALID_ARGUMENT, NULL));
    CHECK(management_command(&f, 0, "!SESSION PING {\"extra\":true}\r", GS232B_ERROR_INVALID_ARGUMENT, NULL));
    CHECK(gs_feed(&f, 0, "@\r") == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs_expect(&f, 0, "?>\r\n"));
    CHECK(f.dispatcher.watchdog_renewed_us == renewed);
    CHECK(gs_run(&f, 0, "L\r", "\r"));
    CHECK(f.dispatcher.watchdog_renewed_us == f.sim.now_us);
    renewed = f.sim.now_us;
    f.sim.now_us += 400000;
    CHECK(management_command(&f, 0, "!SESSION PING\r", GS232B_OK, NULL));
    CHECK(f.dispatcher.watchdog_renewed_us == f.sim.now_us && f.sim.now_us > renewed);
    f.sim.now_us += 1000000;
    CHECK(gs_feed(&f, 0, "!SESSION PING\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_ERROR_TIMEOUT);
    CHECK(stopped(&f.sim) && !gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(!f.io.slots[0].snapshot.connected);
    CHECK(f.dispatcher.control.request_count == 0);
    return true;
}

static bool faults_and_feedback_remain_explicit(void)
{
    gs_fixture_t f; runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(gs_run(&f, 0, "M180\r", "\r"));
    CHECK(management_command(&f, 0, "!FAULT CLEAR\r", GS232B_ERROR_BUSY, NULL));
    f.sim.sample_invalid[0] = true;
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) != GS232B_OK);
    CHECK(f.sim.motion.state.fault.code == GS232B_FAULT_SAMPLE);
    CHECK(management_command(&f, 1, "!MOTION STATUS\r", GS232B_OK, "\"position_mdeg\":null"));
    CHECK(management_command(&f, 0, "!FAULT CLEAR\r", GS232B_ERROR_NOT_READY, NULL));
    CHECK(f.sim.motion.state.fault.code != GS232B_FAULT_NONE);
    f.sim.sample_invalid[0] = false;
    CHECK(management_command(&f, 0, "!FAULT CLEAR\r", GS232B_OK, "\"cleared\":true"));
    CHECK(f.sim.motion.state.fault.code == GS232B_FAULT_NONE);
    CHECK(!f.sim.motion.state.axes[0].target_valid);
    CHECK(stopped(&f.sim) && !gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(gs_feed(&f, 0, "!FAULT CLEAR\rR\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(gs_expect(&f, 0, "!{\"ok\":true,\"cmd\":\"FAULT CLEAR\",\"data\":{\"cleared\":true}}\r\n"));
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(gs_expect(&f, 0, ""));
    CHECK(stopped(&f.sim));
    CHECK(gs_step(&f, 20));
    CHECK(stopped(&f.sim));
    CHECK(management_command(&f, 1, "!MOTION STATUS\r", GS232B_OK, "\"position_valid\":true"));
    return true;
}

int main(void)
{
    return management_matrix_tests() && ownership_status_and_watchdog() &&
        faults_and_feedback_remain_explicit() ? EXIT_SUCCESS : EXIT_FAILURE;
}
