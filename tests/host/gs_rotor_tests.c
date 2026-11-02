#include "management_test_support.h"

static bool selection_and_validation(void)
{
    gs_fixture_t f;
    CHECK(gs_init(&f, true));
    CHECK(management_command(&f, 1, "!ROTOR GET\r", GS232B_OK,
        "\"model_id\":1,\"active_model_id\":1"));
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    const char *const invalid[] = {
        "!ROTOR SET\r", "!ROTOR SET {}\r", "!ROTOR SET {\"model_id\":0}\r",
        "!ROTOR SET {\"model_id\":17}\r", "!ROTOR SET {\"model_id\":-1}\r",
        "!ROTOR SET {\"model_id\":1.5}\r", "!ROTOR SET {\"model_id\":4294967296}\r",
        "!ROTOR SET {\"model_id\":\"5\"}\r", "!ROTOR SET {\"model_id\":null}\r",
        "!ROTOR SET {\"model_id\":true}\r", "!ROTOR SET {\"model_id\":5,\"extra\":1}\r",
        "!ROTOR SET {\"model_id\":5,\"model_id\":6}\r", "!ROTOR GET {\"model_id\":5}\r"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(management_command(&f, 0, invalid[i], GS232B_ERROR_INVALID_ARGUMENT, NULL));
        CHECK(!f.store.records[GS232B_STORE_ROTOR].present && f.dispatcher.rotor_model_id == 1);
    }
    for (uint16_t id = 1; id <= GS232B_ROTOR_MODEL_COUNT; ++id) {
        char line[64];
        snprintf(line, sizeof(line), "!ROTOR SET {\"model_id\":%u}\r", (unsigned)id);
        CHECK(management_command(&f, 0, line, GS232B_OK, id == 1 ?
            "\"restart_required\":false" : "\"restart_required\":true"));
        CHECK(f.dispatcher.rotor_model_id == id && f.dispatcher.active_model_id == 1);
        CHECK(f.sim.motion.config.axes[0].enabled && f.sim.motion.config.axes[1].enabled);
    }
    CHECK(management_command(&f, 2, "!ROTOR GET\r", GS232B_OK,
        "\"model_id\":16,\"active_model_id\":1"));
    const uint64_t revision = f.dispatcher.rotor_revision;
    CHECK(management_command(&f, 0, "!ROTOR SET {\"model_id\":16}\r", GS232B_OK, NULL));
    CHECK(f.dispatcher.rotor_revision == revision);
    CHECK(management_command(&f, 0, "!ROTOR SET {\"model_id\":1}\r", GS232B_OK,
        "\"restart_required\":false"));
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    return true;
}

static bool permissions_and_revalidation(void)
{
    gs_fixture_t f;
    CHECK(gs_init(&f, true));
    char token[65];
    CHECK(management_enable(&f, token));
    for (size_t session = 1; session <= 2; ++session) {
        CHECK(management_command(&f, session, "!ROTOR GET\r", GS232B_OK, NULL));
        CHECK(management_command(&f, session, "!ROTOR SET {\"model_id\":5}\r", GS232B_ERROR_NOT_AUTHORIZED, NULL));
        CHECK(gs_reopen_management(&f, session, (gs232b_transport_t)session));
        CHECK(management_auth(&f, session, token, GS232B_OK));
        CHECK(management_command(&f, session, "!ROTOR SET {\"model_id\":5}\r", GS232B_OK, NULL));
        CHECK(management_command(&f, 0, "!ROTOR SET {\"model_id\":1}\r", GS232B_OK, NULL));
    }
    CHECK(gs_feed(&f, 0, "!ADMIN DISABLE\r") == GS232B_OK);
    CHECK(gs_feed(&f, 1, "!ROTOR SET {\"model_id\":6}\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_ERROR_NOT_AUTHORIZED);
    CHECK(gs_expect(&f, 0, "!{\"ok\":true,\"cmd\":\"ADMIN DISABLE\",\"data\":{\"enabled\":false}}\r\n"));
    CHECK(gs_expect(&f, 1, "!{\"ok\":false,\"cmd\":\"ROTOR SET\",\"error\":\"NOT_AUTHORIZED\"}\r\n"));
    CHECK(f.dispatcher.rotor_model_id == 1);
    runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(management_command(&f, 2, "!ROTOR GET\r", GS232B_ERROR_NOT_AUTHORIZED, NULL));
    CHECK(management_command(&f, 2, "!ROTOR SET {\"model_id\":5}\r", GS232B_ERROR_NOT_AUTHORIZED, NULL));
    return true;
}

static bool busy_and_side_effect_fencing(void)
{
    for (unsigned mode = 0; mode < 6; ++mode) {
        gs_fixture_t f;
        CHECK(gs_init(&f, true));
        char token[65];
        CHECK(management_enable(&f, token) && management_auth(&f, 1, token, GS232B_OK));
        if (mode == 0) CHECK(gs_run(&f, 0, "R\r", "\r"));
        else if (mode == 1 || mode == 2) {
            CHECK(gs_run(&f, 0, "M001 090 180\r", "\r"));
            CHECK(stopped(&f.sim));
            if (mode == 2) CHECK(gs_run(&f, 0, "T\r", "\r"));
        } else if (mode == 3) {
            CHECK(gs_feed(&f, 0, "O\r") == GS232B_OK);
            CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
        } else if (mode == 4) {
            CHECK(gs_feed(&f, 0, "M001 090 ") == GS232B_OK);
            CHECK(gs232b_session_key_valid(f.dispatcher.control.input_reservation.session));
        } else f.sim.manager.state.output.known = false;
        CHECK(management_command(&f, 1, "!ROTOR SET {\"model_id\":5}\r", GS232B_ERROR_BUSY, NULL));
        CHECK(!f.store.records[GS232B_STORE_ROTOR].present && f.dispatcher.rotor_model_id == 1);
    }
    gs_fixture_t f;
    CHECK(gs_init(&f, true));
    gs232b_extension_request_t request;
    const char line[] = "!ROTOR SET {\"model_id\":5}";
    CHECK(gs232b_extension_parse((const uint8_t *)line, strlen(line), &request) == GS232B_OK);
    request.context = gs_context(&f, 0);
    request.transport = GS232B_TRANSPORT_UART1;
    uint8_t tiny[1];
    gs232b_extension_response_t response;
    CHECK(gs232b_extension_dispatch(&f.dispatcher.extensions, &request, tiny, sizeof(tiny), &response) == GS232B_ERROR_BUFFER_TOO_SMALL);
    CHECK(!f.store.records[GS232B_STORE_ROTOR].present);
    CHECK(gs232b_fake_fail_next(&f.store.failures[GS232B_STORE_ROTOR][GS232B_FAKE_STORE_SAVE], GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(management_command(&f, 0, "!ROTOR SET {\"model_id\":5}\r", GS232B_ERROR_IO, NULL));
    CHECK(f.dispatcher.rotor_model_id == 1 && f.dispatcher.rotor_revision == 0);
    CHECK(management_command(&f, 0, "!ROTOR GET\r", GS232B_OK, "\"restart_required\":false"));
    return true;
}

static bool queued_motion_and_stop_recheck(void)
{
    gs_fixture_t f;
    CHECK(gs_init(&f, true));
    char token[65];
    CHECK(management_enable(&f, token) && management_auth(&f, 1, token, GS232B_OK));
    CHECK(gs_feed(&f, 0, "R\r") == GS232B_OK);
    CHECK(gs_feed(&f, 1, "!ROTOR SET {\"model_id\":5}\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_ERROR_BUSY);
    CHECK(gs_expect(&f, 0, "\r"));
    CHECK(gs_expect(&f, 1, "!{\"ok\":false,\"cmd\":\"ROTOR SET\",\"error\":\"BUSY\"}\r\n"));
    CHECK(!f.store.records[GS232B_STORE_ROTOR].present && f.dispatcher.rotor_model_id == 1);
    CHECK(gs_run(&f, 0, "S\r", "\r"));
    CHECK(management_command(&f, 1, "!ROTOR SET {\"model_id\":5}\r", GS232B_OK, "\"restart_required\":true"));
    CHECK(f.dispatcher.active_model_id == 1);
    return true;
}

int main(void)
{
    return selection_and_validation() && permissions_and_revalidation() &&
        busy_and_side_effect_fencing() && queued_motion_and_stop_recheck() ? 0 : 1;
}
