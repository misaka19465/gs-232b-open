#include "gs_test_support.h"

static bool test_owner_loss_clears_sequence_and_calibration(void)
{
    gs_fixture_t f;
    CHECK(gs_init(&f, true));
    CHECK(gs_run(&f, 0, "W010 180 120 200 130\r", "\r"));
    CHECK(gs_feed(&f, 0, "C\rL\rM010 180") == GS232B_OK);
    CHECK(gs232b_dispatcher_input_lost(&f.dispatcher, f.sessions[0]) == GS232B_OK);
    CHECK(stopped(&f.sim));
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(f.dispatcher.control.sequence_state.state == GS232B_SEQUENCE_EMPTY);
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.input_reservation.session));
    CHECK(gs_expect(&f, 0, "AZ=090\r\n?>\r\n"));
    CHECK(gs_run(&f, 0, "R\rC\r", "AZ=090\r\n"));
    CHECK(stopped(&f.sim));
    CHECK(gs_feed(&f, 0, "O\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(f.dispatcher.control.calibration.kind != GS232B_INTERACTION_NONE);
    uint8_t prompt[512];
    size_t prompt_size;
    CHECK(gs232b_fake_sessions_take_tx(&f.io, f.sessions[0], prompt,
        sizeof(prompt), &prompt_size) == GS232B_OK);
    CHECK(prompt_size != 0);
    CHECK(gs232b_dispatcher_input_lost(&f.dispatcher, f.sessions[0]) == GS232B_OK);
    CHECK(f.dispatcher.control.calibration.kind == GS232B_INTERACTION_NONE);
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(gs_expect(&f, 0, "?>\r\n"));
    CHECK(gs_run(&f, 0, "Y\rC\r", "AZ=090\r\n"));
    CHECK(!f.store.records[GS232B_STORE_ZERO_AZ].present);
    return true;
}

static bool test_other_session_loss_only_clears_source(void)
{
    gs_fixture_t f;
    CHECK(gs_init(&f, true));
    CHECK(gs_run(&f, 0, "R\r", "\r"));
    const gs232b_command_epoch_t epoch = f.dispatcher.control.epoch;
    CHECK(gs_feed(&f, 1, "C\rR") == GS232B_OK);
    CHECK(gs232b_dispatcher_input_lost(&f.dispatcher, f.sessions[1]) == GS232B_OK);
    CHECK(gs_expect(&f, 1, "AZ=090\r\n?>\r\n"));
    CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, f.sessions[0]));
    CHECK(f.dispatcher.control.epoch == epoch);
    CHECK(f.dispatcher.control.sessions[1].pending_requests == 0);
    CHECK(f.sim.motion.state.axes[0].mode == GS232B_AXIS_MANUAL ||
        f.sim.motion.state.axes[0].mode == GS232B_AXIS_REVERSAL_WAIT);
    CHECK(gs_run(&f, 1, "R\rC\r", "AZ=090\r\n"));
    CHECK(gs_run(&f, 0, "S\r", "\r"));
    CHECK(gs_feed(&f, 1, "M001 100 120\r") == GS232B_OK);
    CHECK(f.dispatcher.control.upload_queued);
    CHECK(gs232b_dispatcher_input_lost(&f.dispatcher, f.sessions[1]) == GS232B_OK);
    CHECK(!f.dispatcher.control.upload_queued);
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.input_reservation.session));
    CHECK(gs_expect(&f, 1, "?>\r\n"));
    CHECK(gs_run(&f, 2, "R\r", "\r"));
    gs232b_session_key_t stale = f.sessions[1]; ++stale.generation;
    CHECK(gs232b_dispatcher_input_lost(&f.dispatcher, stale) == GS232B_ERROR_STALE_SESSION);
    CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, f.sessions[2]));
    return true;
}

int main(void)
{
    return test_owner_loss_clears_sequence_and_calibration() &&
        test_other_session_loss_only_clears_source() ? EXIT_SUCCESS : EXIT_FAILURE;
}
