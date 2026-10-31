#include "sequence_test_support.h"

static bool test_cross_type_replacement_and_single_clear(void)
{
    command_fixture_t fixture;
    static const char *const old[] = {
        "M001 120 180\r", "W001 120 135 180 150\r"
    };
    static const char *const replacement[] = {
        "M002 100 110\r", "W002 100 105 110 115\r", "M100\r", "W100 105\r"
    };
    for (size_t before = 0; before < 2; ++before) {
        for (size_t after = 0; after < 4; ++after) {
            CHECK(command_fixture_init(&fixture, 0));
            CHECK(command_run(&fixture, 0, old[before]));
            CHECK(command_run(&fixture, 0, "T\r"));
            CHECK(sequence_at(&fixture, 500000));
            CHECK(command_run(&fixture, 0, replacement[after]));
            const bool paired = (after % 2) != 0;
            CHECK(fixture.targets_accepted == 2);
            CHECK(fixture.last_target.targets_mdeg[0] == 100000);
            CHECK(fixture.last_target.axis_mask == (paired ? 3 : 1));
            CHECK(fixture.sim.motion.state.axes[1].target_valid == paired);
            if (!paired) {
                CHECK(fixture.last_target.stop_mask == (before == 1 ? 2 : 0));
                CHECK(fixture.sim.motion.state.axes[1].mode == GS232B_AXIS_IDLE);
            }
            CHECK(sequence_state(&fixture, after < 2 ? GS232B_SEQUENCE_LOADED_WAIT_T :
                GS232B_SEQUENCE_EMPTY, after < 2 ? 1 : 0, after < 2 ? 2 : 0, 0));
            CHECK(sequence_at(&fixture, SECOND_US));
            CHECK(fixture.targets_accepted == 2);
            if (after < 2) {
                CHECK(command_run(&fixture, 0, "T\r"));
                CHECK(sequence_at(&fixture, 3 * SECOND_US));
                CHECK(fixture.targets_accepted == 3);
                CHECK(fixture.last_target.targets_mdeg[0] == 110000);
                CHECK(!paired || fixture.last_target.targets_mdeg[1] == 115000);
                CHECK(sequence_state(&fixture, GS232B_SEQUENCE_COMPLETED, 2, 2, 0));
            }
        }
    }
    return true;
}

static bool test_rejected_replacement_preserves_running_clock(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "M001 090 100 110\r"));
    CHECK(command_run(&fixture, 0, "T\r"));
    const uint16_t *angles = fixture.control.sequence.active.angles;
    CHECK(sequence_at(&fixture, 500000));
    CHECK(command_feed(&fixture, 0, "W010 090 090 180 181\r") ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(command_feed(&fixture, 0, "M000 090 100\r") == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(command_feed(&fixture, 1, "W\r") == GS232B_ERROR_NOT_OWNER);
    CHECK(command_feed(&fixture, 0, "W010 090 090 100 100\r") == GS232B_OK);
    fixture.execute_error = GS232B_ERROR_NOT_READY;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_NOT_READY);
    fixture.execute_error = GS232B_OK;
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_RUNNING, 1, 3, SECOND_US));
    CHECK(fixture.control.sequence.active.angles == angles);
    CHECK(fixture.targets_accepted == 1);
    CHECK(sequence_at(&fixture, SECOND_US));
    CHECK(fixture.targets_accepted == 2 && fixture.last_target.targets_mdeg[0] == 100000);
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_RUNNING, 2, 3, 2 * SECOND_US));
    return true;
}

static bool test_manual_and_stop_clear_rules(void)
{
    command_fixture_t fixture;
    static const char *const commands[] = {"R\r", "L\r", "U\r", "D\r", "A\r", "E\r",
                                           "S\r", "M\r", "W   \r"};
    for (size_t index = 0; index < sizeof(commands) / sizeof(commands[0]); ++index) {
        CHECK(command_fixture_init(&fixture, 0));
        CHECK(command_run(&fixture, 0, "W001 120 120 180 150\r"));
        CHECK(command_run(&fixture, 0, "T\r"));
        CHECK(command_feed(&fixture, 0, commands[index]) == GS232B_OK);
        CHECK(gs232b_control_tick(&fixture.control) ==
              (index >= 7 ? GS232B_ERROR_INVALID_ARGUMENT : GS232B_OK));
        CHECK(sequence_state(&fixture, GS232B_SEQUENCE_EMPTY, 0, 0, 0));
        CHECK(fixture.control.sequence.active.angles == NULL);
        if (index < 6) {
            const size_t axis = index < 2 || index == 4 ? 0 : 1;
            CHECK(fixture.sim.motion.axes[1 - axis].intent == GS232B_AXIS_GOTO);
            CHECK(fixture.sim.motion.axes[axis].intent ==
                  (index < 4 ? GS232B_AXIS_MANUAL : GS232B_AXIS_IDLE));
        } else {
            CHECK(stopped(&fixture.sim));
        }
        CHECK(sequence_at(&fixture, SECOND_US));
        CHECK(fixture.targets_accepted == 1 && fixture.sequence_targets == 0);
        CHECK(sequence_query(&fixture, 1, "=0000=0000\r\n"));
    }
    return true;
}

static bool test_queries_speed_and_partial_upload_do_not_block_sequence(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "M001 090 090 090 090\r"));
    CHECK(command_run(&fixture, 0, "T\r"));
    static const char *const commands[] = {
        "C\r", "B\r", "C2\r", "N\r", "H\r", "H2\r", "H3\r", "\r", "X1\r"
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        CHECK(command_run(&fixture, i == 8 ? 0 : 1, commands[i]));
        CHECK(sequence_state(&fixture, GS232B_SEQUENCE_RUNNING, 1, 4, SECOND_US));
    }
    CHECK(fixture.sim.motion.config.azimuth_speed_permille == 250);
    const char upload[] = "W009 090 090 090 090";
    for (size_t offset = 0; offset < sizeof(upload) - 1; offset += 2) {
        CHECK(gs232b_control_feed(&fixture.control, command_context(&fixture, 0),
            (const uint8_t *)upload + offset, 2) == GS232B_OK);
        CHECK(sequence_at(&fixture, (offset / 2 + 1) * 500000));
        CHECK(command_run(&fixture, 1, "N\r"));
    }
    CHECK(fixture.targets_accepted == 4 && fixture.sequence_targets == 3);
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_COMPLETED, 4, 4, 0));
    CHECK(sequence_response(&fixture, 1, "=0004=0004\r\n"));
    CHECK(gs232b_session_key_valid(fixture.control.input_reservation.session));
    CHECK(command_owner(&fixture, 0));
    CHECK(command_run(&fixture, 0, "\r"));
    CHECK(fixture.targets_accepted == 5);
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_LOADED_WAIT_T, 1, 2, 0));
    CHECK(sequence_query(&fixture, 1, "=0001=0002\r\n"));
    return true;
}

static bool test_disabled_elevation_and_compiled_p36_targets(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    gs232b_motion_axis_config_t config = fixture.sim.motion.config.axes[1];
    config.enabled = false;
    CHECK(gs232b_motion_configure_axis(&fixture.sim.motion, GS232B_AXIS_ELEVATION,
                                       &config) == GS232B_OK);
    CHECK(command_run(&fixture, 0, "W001 090 000 100 000\r"));
    CHECK(command_run(&fixture, 0, "T\r"));
    CHECK(sequence_at(&fixture, SECOND_US));
    CHECK(fixture.last_target.axis_mask == GS232B_MOTION_AZ_MASK);
    CHECK(fixture.last_target.targets_mdeg[0] == 100000);
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_COMPLETED, 2, 2, 0));

    CHECK(command_fixture_init(&fixture, 0));
    fixture.display.mode = GS232B_AZIMUTH_DISPLAY_P36;
    fixture.display.center = GS232B_AZIMUTH_CENTER_SOUTH;
    fixture.sim.position_udeg[0] = INT64_C(400000000);
    config = fixture.sim.motion.config.axes[0];
    CHECK(gs232b_motion_configure_axis(&fixture.sim.motion, GS232B_AXIS_AZIMUTH,
                                       &config) == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(command_run(&fixture, 0, "M001 060 090 080\r"));
    CHECK(fixture.last_target.targets_mdeg[0] == 420000);
    CHECK(command_run(&fixture, 0, "T\r"));
    CHECK(sequence_at(&fixture, SECOND_US));
    CHECK(fixture.last_target.targets_mdeg[0] == 450000);
    CHECK(sequence_at(&fixture, 2 * SECOND_US));
    CHECK(fixture.last_target.targets_mdeg[0] == 440000);
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_COMPLETED, 3, 3, 0));
    return true;
}

static bool test_display_changes_clear_only_on_successful_effective_change(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "M001 090 090\r"));
    CHECK(command_run(&fixture, 0, "P45\r"));
    CHECK(command_run(&fixture, 0, "Z\r"));
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_LOADED_WAIT_T, 1, 2, 0));
    CHECK(command_feed(&fixture, 0, "P36\r") == GS232B_OK);
    fixture.execute_error = GS232B_ERROR_IO;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_IO);
    fixture.execute_error = GS232B_OK;
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_LOADED_WAIT_T, 1, 2, 0));
    CHECK(fixture.display.mode == GS232B_AZIMUTH_DISPLAY_P45);
    CHECK(command_run(&fixture, 0, "T\r"));
    CHECK(command_run(&fixture, 0, "P45\r"));
    CHECK(command_run(&fixture, 0, "Z\r"));
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_RUNNING, 1, 2, SECOND_US));
    CHECK(command_feed(&fixture, 0, "P36\r") == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_BUSY);
    CHECK(sequence_at(&fixture, SECOND_US));
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_COMPLETED, 2, 2, 0));
    CHECK(command_run(&fixture, 0, "P36\r"));
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_EMPTY, 0, 0, 0));
    CHECK(command_run(&fixture, 0, "M001 270 270\r"));
    CHECK(command_run(&fixture, 0, "P36\r"));
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_LOADED_WAIT_T, 1, 2, 0));
    CHECK(command_run(&fixture, 0, "Z\r"));
    CHECK(sequence_state(&fixture, GS232B_SEQUENCE_EMPTY, 0, 0, 0));
    CHECK(sequence_query(&fixture, 1, "=0000=0000\r\n"));
    return true;
}

int main(void)
{
    if (!test_cross_type_replacement_and_single_clear() ||
        !test_rejected_replacement_preserves_running_clock() ||
        !test_manual_and_stop_clear_rules() ||
        !test_queries_speed_and_partial_upload_do_not_block_sequence() ||
        !test_disabled_elevation_and_compiled_p36_targets() ||
        !test_display_changes_clear_only_on_successful_effective_change()) {
        return EXIT_FAILURE;
    }
    puts("sequence replacement, clear rules, M/W mixing and nonblocking reception passed");
    return EXIT_SUCCESS;
}
