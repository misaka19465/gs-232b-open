#include "command_test_support.h"

static size_t make_line(char *line, char command, size_t points)
{
    memcpy(line, command == 'M' ? "M010" : "W010", 4);
    size_t used = 4;
    for (size_t i = 0; i < points; ++i) {
        const unsigned az = i == 0 ? 120U : (unsigned)(i * 37U % 451U);
        const unsigned el = i == 0 ? 135U : (unsigned)(i * 23U % 181U);
        used += (size_t)sprintf(line + used, " %03u", az);
        if (command == 'W') {
            used += (size_t)sprintf(line + used, " %03u", el);
        }
    }
    return used;
}

static gs232b_error_t feed_fragments(command_fixture_t *fixture, size_t session,
                                     const char *bytes, size_t size, bool slow)
{
    static const size_t chunks[] = {1, 2, 3, 7, 31, 127, 256};
    gs232b_error_t result = GS232B_OK;
    for (size_t offset = 0, chunk = 0; offset < size; ++chunk) {
        size_t amount = chunks[chunk % (sizeof(chunks) / sizeof(chunks[0]))];
        if (amount > size - offset) {
            amount = size - offset;
        }
        if (slow) {
            fixture->sim.now_us += UINT64_C(1000000);
        }
        const gs232b_error_t error = gs232b_control_feed(&fixture->control,
            command_context(fixture, session), (const uint8_t *)bytes + offset, amount);
        if (result == GS232B_OK) {
            result = error;
        }
        if (slow && gs232b_control_tick(&fixture->control) != GS232B_OK) {
            return GS232B_ERROR_HARDWARE_FAULT;
        }
        offset += amount;
    }
    return result;
}

static uint32_t sequence_hash(const gs232b_sequence_data_t *command)
{
    uint32_t hash = UINT32_C(2166136261);
    size_t count = command->point_count * (command->command == 'W' ? 2U : 1U);
    for (size_t i = 0; i < count; ++i) {
        hash = (hash ^ command->angles[i]) * UINT32_C(16777619);
    }
    return hash;
}

static bool rejected_keeps_active(command_fixture_t *fixture, const char *bytes,
                                   size_t size, gs232b_error_t expected)
{
    const gs232b_sequence_data_t before = fixture->control.sequence.active;
    const gs232b_sequence_state_t state_before = fixture->control.sequence_state.state;
    const uint32_t hash = sequence_hash(&before);
    gs232b_motion_snapshot_t motion_before, motion_after;
    CHECK(snapshot(&fixture->sim, &motion_before));
    const size_t completed = fixture->completes[0];
    const size_t executed = fixture->executed;
    CHECK(feed_fragments(fixture, 0, bytes, size, false) == expected);
    CHECK(fixture->completes[0] == completed + 1);
    CHECK(fixture->last_error[0] == expected);
    CHECK(gs232b_control_tick(&fixture->control) == GS232B_OK);
    CHECK(fixture->executed == executed);
    CHECK(fixture->control.sequence.active.angles == before.angles);
    CHECK(fixture->control.sequence.active.command == before.command);
    CHECK(fixture->control.sequence.active.point_count == before.point_count);
    CHECK(fixture->control.sequence.active.interval_s == before.interval_s);
    CHECK(fixture->control.sequence_state.state == state_before);
    CHECK(sequence_hash(&fixture->control.sequence.active) == hash);
    CHECK(!gs232b_session_key_valid(fixture->control.input_reservation.session));
    CHECK(snapshot(&fixture->sim, &motion_after));
    for (size_t axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        CHECK(motion_after.axes[axis].target_valid == motion_before.axes[axis].target_valid);
        CHECK(motion_after.axes[axis].target_mdeg == motion_before.axes[axis].target_mdeg);
    }
    return true;
}

static bool test_maxima_and_atomic_replacement(void)
{
    command_fixture_t fixture;
    char line[GS232B_GS_LINE_MAX_BYTES + 2];
    CHECK(command_fixture_init(&fixture, 0));
    size_t size = make_line(line, 'M', GS232B_M_SEQUENCE_MAX_POINTS);
    CHECK(size == 15204);
    CHECK(feed_fragments(&fixture, 0, line, size, true) == GS232B_OK);
    CHECK(fixture.sim.now_us > UINT64_C(127000000));
    CHECK(command_unowned(&fixture));
    CHECK(fixture.executed == 0 && fixture.control.request_count == 0);
    CHECK(fixture.control.sequence.active.point_count == 0);
    CHECK(stopped(&fixture.sim));
    CHECK(!fixture.sim.motion.state.axes[0].target_valid);
    CHECK(command_feed(&fixture, 0, "\r") == GS232B_OK);
    CHECK(stopped(&fixture.sim));
    CHECK(command_unowned(&fixture));
    CHECK(fixture.control.upload_queued && fixture.control.request_bytes_used == 0);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(command_owner(&fixture, 0));
    const gs232b_sequence_data_t *active = &fixture.control.sequence.active;
    CHECK(active->command == 'M' && active->point_count == 3800);
    CHECK(active->interval_s == 10 && active->angles[0] == 120);
    for (size_t i = 1; i < 3800; ++i) {
        CHECK(active->angles[i] == i * 37U % 451U);
    }
    const uint16_t *old_angles = active->angles;
    const uint32_t old_hash = sequence_hash(active);
    const int32_t old_target = fixture.sim.motion.state.axes[0].target_mdeg;
    size = make_line(line, 'W', GS232B_W_SEQUENCE_MAX_PAIRS);
    CHECK(size == 15204);
    CHECK(feed_fragments(&fixture, 0, line, size, false) == GS232B_OK);
    CHECK(active->angles == old_angles && sequence_hash(active) == old_hash);
    CHECK(fixture.sim.motion.state.axes[0].target_mdeg == old_target);
    CHECK(!fixture.sim.motion.state.axes[1].target_valid);
    CHECK(command_feed(&fixture, 0, "\r\n") == GS232B_OK);
    CHECK(active->angles == old_angles && sequence_hash(active) == old_hash);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(active->command == 'W' && active->point_count == 1900);
    CHECK(active->angles != old_angles);
    CHECK(active->angles[0] == 120 && active->angles[1] == 135);
    for (size_t i = 1; i < 1900; ++i) {
        CHECK(active->angles[2 * i] == i * 37U % 451U);
        CHECK(active->angles[2 * i + 1] == i * 23U % 181U);
    }
    CHECK(fixture.sim.motion.state.axes[1].target_mdeg == 135000);
    CHECK(fixture.control.sequence_state.state == GS232B_SEQUENCE_LOADED_WAIT_T);
    CHECK(!gs232b_session_key_valid(fixture.control.input_reservation.session));
    return true;
}

static bool test_invalid_uploads_preserve_old_sequence(void)
{
    command_fixture_t fixture;
    char line[GS232B_GS_LINE_MAX_BYTES + 2];
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "W007 090 090 180 045 270 010\r"));
    CHECK(command_run(&fixture, 0, "T\r"));
    const char commands[] = {'M', 'W'};
    for (size_t i = 0; i < 2; ++i) {
        const size_t maximum = i == 0 ? 3800 : 1900;
        size_t size = make_line(line, commands[i], maximum + 1);
        line[size++] = '\r';
        CHECK(rejected_keeps_active(&fixture, line, size, GS232B_ERROR_RESOURCE_EXHAUSTED));
        size = make_line(line, commands[i], maximum);
        memcpy(line + size - 3, i == 0 ? "451" : "181", 3);
        line[size++] = '\r';
        CHECK(rejected_keeps_active(&fixture, line, size, GS232B_ERROR_INVALID_ARGUMENT));
    }
    static const char *const invalid[] = {
        "M010 100\r", "M010 100 20\r", "M010 100 +20\r",
        "W010 100 020 200\r", "W010 100 020 200 030 250\r",
        "M000 100 200\r", "W000 100 020 200 030\r", "M010 100 S\r"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(rejected_keeps_active(&fixture, invalid[i], strlen(invalid[i]),
                                     GS232B_ERROR_INVALID_ARGUMENT));
    }
    size_t size = make_line(line, 'M', 3800);
    memset(line + size, ' ', GS232B_GS_LINE_MAX_BYTES - size);
    line[GS232B_GS_LINE_MAX_BYTES] = '\r';
    CHECK(feed_fragments(&fixture, 0, line, GS232B_GS_LINE_MAX_BYTES + 1, false) == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.control.sequence.active.point_count == 3800);
    line[GS232B_GS_LINE_MAX_BYTES] = ' ';
    line[GS232B_GS_LINE_MAX_BYTES + 1] = '\r';
    CHECK(rejected_keeps_active(&fixture, line, sizeof(line), GS232B_ERROR_RESOURCE_EXHAUSTED));
    return true;
}

static bool test_timeout_and_discard_recovery(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_run(&fixture, 0, "M010 090 100\r"));
    const gs232b_sequence_data_t old = fixture.control.sequence.active;
    const uint32_t hash = sequence_hash(&old);
    CHECK(command_feed(&fixture, 0, "W010 180 030 270 ") == GS232B_OK);
    const size_t completed = fixture.completes[0];
    fixture.sim.now_us += GS232B_LINE_PARTIAL_TIMEOUT_US - 1;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.completes[0] == completed);
    ++fixture.sim.now_us;
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.completes[0] == completed + 1);
    CHECK(fixture.last_error[0] == GS232B_ERROR_TIMEOUT);
    CHECK(fixture.control.sequence.active.angles == old.angles);
    CHECK(sequence_hash(&fixture.control.sequence.active) == hash);
    CHECK(fixture.control.sequence_state.state == GS232B_SEQUENCE_LOADED_WAIT_T);
    CHECK(!gs232b_session_key_valid(fixture.control.input_reservation.session));
    CHECK(command_run(&fixture, 1, "N\r"));
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(command_feed(&fixture, 0, "S\r\n") == GS232B_OK);
    CHECK(fixture.completes[0] == completed + 1);
    CHECK(sequence_hash(&fixture.control.sequence.active) == hash);
    CHECK(command_run(&fixture, 0, "M010 090 200\r"));
    CHECK(fixture.control.sequence.active.angles != old.angles);
    return true;
}

static bool test_reservation_queries_queue_and_stop(void)
{
    command_fixture_t fixture;
    CHECK(command_fixture_init(&fixture, 0));
    CHECK(command_feed(&fixture, 1, "R\r") == GS232B_OK);
    CHECK(command_feed(&fixture, 0, "m010 120 ") == GS232B_OK);
    CHECK(command_unowned(&fixture));
    CHECK(gs232b_control_take(&fixture.control, command_context(&fixture, 1)) == GS232B_ERROR_BUSY);
    CHECK(command_feed(&fixture, 1, "U\r") == GS232B_ERROR_BUSY);
    CHECK(command_feed(&fixture, 1, "W010 100 020 200 030\r") == GS232B_ERROR_BUSY);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_ERROR_BUSY);
    CHECK(fixture.executed == 0 && stopped(&fixture.sim));
    CHECK(command_run(&fixture, 2, "C2\r"));
    CHECK(command_run(&fixture, 1, "N\r"));
    CHECK(command_unowned(&fixture));
    CHECK(command_feed(&fixture, 0, "240\r") == GS232B_OK);
    CHECK(command_feed(&fixture, 0, "M010 200 300\r") == GS232B_ERROR_BUSY);
    CHECK(fixture.control.upload_queued);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(fixture.control.sequence.active.angles[0] == 120);
    CHECK(fixture.control.sequence.active.angles[1] == 240);
    CHECK(command_feed(&fixture, 1, "M\r") == GS232B_ERROR_NOT_OWNER);
    CHECK(fixture.control.sequence.active.point_count == 2);
    CHECK(command_feed(&fixture, 0, "W010 100 020 200 ") == GS232B_OK);
    const gs232b_command_context_t old_rx = command_context(&fixture, 0);
    CHECK(command_run(&fixture, 2, "S\r"));
    CHECK(stopped(&fixture.sim));
    CHECK(fixture.control.sequence.active.point_count == 0);
    CHECK(!gs232b_session_key_valid(fixture.control.input_reservation.session));
    CHECK(command_feed(&fixture, 0, "030\r") == GS232B_OK);
    static const uint8_t stale[] = "M010 100 200\r";
    CHECK(gs232b_control_feed(&fixture.control, old_rx, stale, sizeof(stale) - 1) ==
          GS232B_ERROR_CONFLICT);
    CHECK(gs232b_control_tick(&fixture.control) == GS232B_OK);
    CHECK(stopped(&fixture.sim));
    CHECK(fixture.control.sequence.active.point_count == 0);
    return true;
}

int main(void)
{
    if (!test_maxima_and_atomic_replacement() ||
        !test_invalid_uploads_preserve_old_sequence() ||
        !test_timeout_and_discard_recovery() ||
        !test_reservation_queries_queue_and_stop()) {
        return EXIT_FAILURE;
    }
    puts("maximum sequence uploads, atomic replacement and reservation tests passed");
    return EXIT_SUCCESS;
}
