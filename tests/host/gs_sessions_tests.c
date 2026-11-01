#include "gs_test_support.h"
#include "gs_help_fixtures.h"

static bool test_help_and_three_sources(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_feed(&fixture, 0, "H\rC\r") == GS232B_OK);
    CHECK(gs_feed(&fixture, 1, "h2\rB\r") == GS232B_OK);
    CHECK(gs_feed(&fixture, 2, "h3\rC2\r") == GS232B_OK);
    CHECK(gs_drain(&fixture));
    char expected[1024];
    snprintf(expected, sizeof(expected), "%sAZ=090\r\n", gs_help_azimuth);
    CHECK(gs_expect(&fixture, 0, expected));
    snprintf(expected, sizeof(expected), "%sEL=090\r\n", gs_help_elevation);
    CHECK(gs_expect(&fixture, 1, expected));
    CHECK(gs_expect(&fixture, 2, GS_HELP_DISPLAY_PREFIX
        "mode 450 Degree\r\nN Center\r\nAZ=090  EL=090\r\n"));
    CHECK(gs_run(&fixture, 0, "P36\r", "\r"));
    CHECK(gs_run(&fixture, 1, "H3\r", GS_HELP_DISPLAY_PREFIX "mode 360 Degree\r\nN Center\r\n"));
    CHECK(gs_run(&fixture, 0, "Z\r", "\r"));
    CHECK(gs_run(&fixture, 2, "H3\r", GS_HELP_DISPLAY_PREFIX "mode 360 Degree\r\nS Center\r\n"));
    CHECK(gs_quiet(&fixture));
    return true;
}

static bool test_errors_do_not_overtake_queries(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_run(&fixture, 0, "R\r", "\r"));
    CHECK(gs_feed(&fixture, 1, "C\rR\rB\rY1\rC2\r") == GS232B_ERROR_NOT_OWNER);
    CHECK(gs_expect(&fixture, 1, ""));
    CHECK(gs_drain(&fixture));
    CHECK(gs_expect(&fixture, 1, "AZ=090\r\n?>\r\nEL=090\r\n?>\r\nAZ=090  EL=090\r\n"));
    CHECK(gs_run(&fixture, 2, "S\r", "\r"));
    static const uint8_t malformed[] = {'C', '\r', 'M', '1', '0', '0', 0, '\r', 'B', '\r'};
    CHECK(gs232b_dispatcher_feed(&fixture.dispatcher, gs_context(&fixture, 1),
        malformed, sizeof(malformed)) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs_expect(&fixture, 1, ""));
    CHECK(gs_drain(&fixture));
    CHECK(gs_expect(&fixture, 1, "AZ=090\r\n?>\r\nEL=090\r\n"));
    CHECK(stopped(&fixture.sim));
    CHECK(gs_quiet(&fixture));
    return true;
}

static bool test_stop_action_is_first_but_response_order_is_preserved(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_run(&fixture, 0, "W010 180 120 200 130\r", "\r"));
    CHECK(gs_feed(&fixture, 0, "R\rL\rU\r") == GS232B_OK);
    CHECK(gs_feed(&fixture, 1, "C\r@\rB\rS\rC2\rN\rA\rC\rE\rH3\r") ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs_expect(&fixture, 1, ""));
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
    CHECK(stopped(&fixture.sim));
    CHECK(fixture.dispatcher.control.request_count == 0);
    CHECK(gs_expect(&fixture, 1,
        "AZ=090\r\n?>\r\nEL=090\r\n\rAZ=090  EL=090\r\n=0000=0000\r\n\rAZ=090\r\n\r"
        GS_HELP_DISPLAY_PREFIX "mode 450 Degree\r\nN Center\r\n"));
    CHECK(gs_step(&fixture, 15));
    CHECK(stopped(&fixture.sim));
    CHECK(gs_quiet(&fixture));
    return true;
}

static bool test_execution_rechecks_owner_before_writes(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    static const char *const unsupported[] = {
        "!OWNER TAKE\r", "!OWNER GET\r", "!OWNER RELEASE\r", "!NET GET\r"
    };
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        CHECK(gs_feed(&fixture, 0, unsupported[i]) == GS232B_ERROR_NOT_FOUND);
        gs232b_extension_request_t request;
        CHECK(gs232b_extension_parse((const uint8_t *)unsupported[i],
            strlen(unsupported[i]) - 1U, &request) == GS232B_OK);
        char expected[160];
        snprintf(expected, sizeof(expected),
            "!{\"ok\":false,\"cmd\":\"%s\",\"error\":\"NOT_FOUND\"}\r\n", request.cmd);
        CHECK(gs_expect(&fixture, 0, expected));
        CHECK(!gs232b_session_key_valid(fixture.dispatcher.control.owner));
    }
    CHECK(gs_feed(&fixture, 0, "R\r") == GS232B_OK);
    CHECK(gs_feed(&fixture, 1, "P36\rX4\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
    CHECK(gs_expect(&fixture, 0, "\r"));
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_ERROR_NOT_OWNER);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_ERROR_NOT_OWNER);
    CHECK(gs_expect(&fixture, 1, "?>\r\n?>\r\n"));
    CHECK(fixture.dispatcher.display_revision == 0);
    CHECK(fixture.dispatcher.display.mode == GS232B_AZIMUTH_DISPLAY_P45);
    CHECK(fixture.sim.motion.config.azimuth_speed_permille == 800);
    CHECK(!fixture.store.records[GS232B_STORE_DISPLAY].present);
    CHECK(gs_run(&fixture, 2, "S\r", "\r"));
    return true;
}

static bool test_tx_failure_and_generation_reuse(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_run(&fixture, 0, "R\r", "\r"));
    const gs232b_command_context_t old = gs_context(&fixture, 0);
    CHECK(gs232b_fake_sessions_fail_next(&fixture.io, fixture.sessions[0],
        GS232B_FAKE_SESSION_SEND, GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs_feed(&fixture, 0, "C\rU\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_ERROR_IO);
    CHECK(stopped(&fixture.sim));
    CHECK(!gs232b_session_key_valid(fixture.dispatcher.control.owner));
    CHECK(!fixture.io.slots[0].snapshot.connected);
    CHECK(gs_open(&fixture, 0, GS232B_TRANSPORT_UART1));
    CHECK(fixture.sessions[0].generation > old.session.generation);
    static const uint8_t stale[] = "R\rS\r";
    CHECK(gs232b_dispatcher_feed(&fixture.dispatcher, old, stale, sizeof(stale) - 1U) ==
          GS232B_ERROR_STALE_SESSION);
    CHECK(gs_run(&fixture, 1, "U\r", "\r"));
    CHECK(gs232b_dispatcher_session_close(&fixture.dispatcher, old.session) ==
          GS232B_ERROR_STALE_SESSION);
    CHECK(gs_run(&fixture, 0, "C2\r", "AZ=090  EL=090\r\n"));
    CHECK(gs_run(&fixture, 2, "S\r", "\r"));
    CHECK(gs_quiet(&fixture));
    return true;
}

static bool test_bounded_reply_queue_never_blocks_stop(void)
{
    gs_fixture_t fixture;
    CHECK(gs_init(&fixture, true));
    CHECK(gs_run(&fixture, 0, "R\r", "\r"));
    char input[64];
    for (size_t i = 0; i < 20; ++i) {
        input[i * 2] = 'S';
        input[i * 2 + 1] = '\r';
    }
    input[40] = '\0';
    CHECK(gs_feed(&fixture, 1, input) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(!fixture.io.slots[1].snapshot.connected);
    CHECK(gs232b_dispatcher_tick(&fixture.dispatcher) == GS232B_OK);
    CHECK(stopped(&fixture.sim));
    CHECK(gs_expect(&fixture, 0, ""));
    CHECK(gs_expect(&fixture, 2, ""));
    return true;
}

int main(void)
{
    if (!test_help_and_three_sources() || !test_errors_do_not_overtake_queries() ||
        !test_stop_action_is_first_but_response_order_is_preserved() ||
        !test_execution_rechecks_owner_before_writes() ||
        !test_tx_failure_and_generation_reuse() || !test_bounded_reply_queue_never_blocks_stop()) {
        return EXIT_FAILURE;
    }
    puts("GS source isolation, ordered replies, stop priority and TX lifecycle passed");
    return EXIT_SUCCESS;
}
