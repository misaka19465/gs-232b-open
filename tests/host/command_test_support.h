#ifndef GS232B_COMMAND_TEST_SUPPORT_H
#define GS232B_COMMAND_TEST_SUPPORT_H

#include "gs232b/command_control.h"
#include "gs232b/response_encoder.h"
#include "gs232b/fakes.h"
#include "motion_test_support.h"

#include <string.h>

#define CONTROL_TEST_SESSIONS 3U

typedef struct {
    gs232b_motion_sim_t sim;
    gs232b_control_t control;
    uint8_t request_bytes[32768];
    uint8_t line_storage[CONTROL_TEST_SESSIONS][GS232B_LINE_STORAGE_BYTES];
    gs232b_session_key_t sessions[CONTROL_TEST_SESSIONS];
    gs232b_fake_store_t store;
    gs232b_azimuth_display_settings_t display;
    gs232b_error_t validation_error;
    gs232b_error_t execute_error;
    gs232b_error_t snapshot_error;
    gs232b_error_t sequence_error;
    gs232b_error_t respond_error;
    gs232b_time_us_t tick_latency_us;
    size_t target_attempts;
    size_t targets_accepted;
    size_t sequence_targets;
    size_t faults;
    gs232b_position_plan_t last_target;
    gs232b_time_us_t target_at_us;
    size_t executed;
    size_t queries;
    size_t cancels;
    size_t ticks;
    size_t completes[CONTROL_TEST_SESSIONS];
    gs232b_error_t last_error[CONTROL_TEST_SESSIONS];
    gs232b_request_access_t last_access[CONTROL_TEST_SESSIONS];
    uint8_t response[CONTROL_TEST_SESSIONS][128];
    size_t response_size[CONTROL_TEST_SESSIONS];
    size_t async_responses[CONTROL_TEST_SESSIONS];
} command_fixture_t;

static inline gs232b_time_us_t control_clock(void *ctx)
{
    return ((command_fixture_t *)ctx)->sim.now_us;
}

static inline bool command_is(const gs232b_control_request_t *request, const char *text)
{
    return gs232b_byte_span_equal_ascii_ci(request->line, text);
}

static inline gs232b_error_t control_validate(void *ctx,
                                               const gs232b_control_request_t *request)
{
    command_fixture_t *fixture = ctx;
    (void)request;
    return fixture->validation_error;
}

static inline gs232b_error_t control_execute(void *ctx,
                                              const gs232b_control_request_t *request)
{
    command_fixture_t *fixture = ctx;
    ++fixture->executed;
    if (fixture->execute_error != GS232B_OK) {
        return fixture->execute_error;
    }
    if (request->access == GS232B_REQUEST_QUERY) {
        ++fixture->queries;
        return GS232B_OK;
    }
    if (request->position != NULL) {
        const gs232b_position_plan_t *plan = request->position_plan;
        ++fixture->target_attempts;
        gs232b_error_t error = GS232B_OK;
        if (plan->stop_mask != 0) {
            error = gs232b_motion_stop(&fixture->sim.motion, plan->stop_mask);
        }
        if (error == GS232B_OK) {
            error = gs232b_motion_goto(&fixture->sim.motion, plan->axis_mask,
                                        plan->targets_mdeg);
        }
        if (error == GS232B_OK) {
            ++fixture->targets_accepted;
            fixture->last_target = *plan;
            fixture->target_at_us = fixture->sim.now_us;
        }
        return error;
    }
    if (command_is(request, "R") || command_is(request, "L")) {
        return gs232b_motion_manual(&fixture->sim.motion, GS232B_AXIS_AZIMUTH,
            command_is(request, "R") ? GS232B_MOTION_POSITIVE : GS232B_MOTION_NEGATIVE);
    }
    if (command_is(request, "U") || command_is(request, "D")) {
        return gs232b_motion_manual(&fixture->sim.motion, GS232B_AXIS_ELEVATION,
            command_is(request, "U") ? GS232B_MOTION_POSITIVE : GS232B_MOTION_NEGATIVE);
    }
    if (command_is(request, "X1")) {
        return gs232b_motion_set_speed(&fixture->sim.motion, 250);
    }
    if (command_is(request, "P36") || command_is(request, "P45")) {
        fixture->display.mode = command_is(request, "P36") ?
            GS232B_AZIMUTH_DISPLAY_P36 : GS232B_AZIMUTH_DISPLAY_P45;
    } else if (command_is(request, "Z") &&
               fixture->display.mode == GS232B_AZIMUTH_DISPLAY_P36) {
        fixture->display.center = fixture->display.center == GS232B_AZIMUTH_CENTER_NORTH ?
            GS232B_AZIMUTH_CENTER_SOUTH : GS232B_AZIMUTH_CENTER_NORTH;
    }
    return GS232B_OK;
}

static inline void control_cancel(void *ctx)
{
    command_fixture_t *fixture = ctx;
    ++fixture->cancels;
}

static inline gs232b_error_t control_stop(void *ctx, uint8_t mask)
{
    return gs232b_motion_stop(&((command_fixture_t *)ctx)->sim.motion, mask);
}

static inline gs232b_error_t control_executor_tick(void *ctx)
{
    command_fixture_t *fixture = ctx;
    ++fixture->ticks;
    fixture->sim.now_us += fixture->tick_latency_us;
    return gs232b_motion_tick(&fixture->sim.motion);
}

static inline gs232b_error_t control_sequence_target(void *ctx,
    const gs232b_position_plan_t *plan)
{
    command_fixture_t *fixture = ctx;
    ++fixture->target_attempts;
    const gs232b_error_t error = fixture->sequence_error != GS232B_OK ?
        fixture->sequence_error : gs232b_motion_goto(&fixture->sim.motion,
            plan->axis_mask, plan->targets_mdeg);
    if (error == GS232B_OK) {
        ++fixture->targets_accepted;
        ++fixture->sequence_targets;
        fixture->last_target = *plan;
        fixture->target_at_us = fixture->sim.now_us;
    }
    return error;
}

static inline gs232b_error_t control_fault(void *ctx, gs232b_fault_code_t code,
                                           gs232b_error_t error)
{
    command_fixture_t *fixture = ctx;
    ++fixture->faults;
    return gs232b_motion_abort(&fixture->sim.motion, code, error);
}

static inline gs232b_error_t control_activity(void *ctx,
                                               gs232b_control_activity_t *activity)
{
    command_fixture_t *fixture = ctx;
    if (fixture->snapshot_error != GS232B_OK) {
        return fixture->snapshot_error;
    }
    gs232b_motion_snapshot_t state;
    const gs232b_error_t error = gs232b_motion_snapshot(&fixture->sim.motion, &state);
    if (error != GS232B_OK) {
        return error;
    }
    memset(activity, 0, sizeof(*activity));
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        const gs232b_axis_mode_t mode = state.axes[axis].mode;
        activity->axes_active[axis] = mode == GS232B_AXIS_MANUAL ||
                                     mode == GS232B_AXIS_GOTO ||
                                     mode == GS232B_AXIS_REVERSAL_WAIT;
    }
    activity->output_stopped = state.output.known &&
        state.output.last_successful.azimuth == GS232B_AZ_STOP &&
        state.output.last_successful.elevation == GS232B_EL_STOP &&
        state.output.last_successful.speed_permille == 0;
    activity->faulted = state.fault.code != GS232B_FAULT_NONE;
    return GS232B_OK;
}

static inline void control_complete(void *ctx, const gs232b_control_request_t *request,
                                      gs232b_error_t error, uint32_t count)
{
    command_fixture_t *fixture = ctx;
    const size_t index = request->context.session.session_id - 1;
    if (index < CONTROL_TEST_SESSIONS) {
        fixture->completes[index] += count;
        fixture->last_error[index] = error;
        fixture->last_access[index] = request->access;
        gs232b_response_t response = {
            .kind = error == GS232B_OK ? GS232B_RESPONSE_SUCCESS : GS232B_RESPONSE_ERROR
        };
        if (error == GS232B_OK && request->response != NULL) {
            response = *request->response;
        }
        if (error == GS232B_OK && request->access == GS232B_REQUEST_QUERY &&
            command_is(request, "N")) {
            gs232b_control_snapshot_t state;
            if (gs232b_control_snapshot(&fixture->control, &state) == GS232B_OK) {
                response.kind = GS232B_RESPONSE_SEQUENCE;
                response.data.sequence.current_point = state.sequence.current_point;
                response.data.sequence.point_count = state.sequence.point_count;
            }
        }
        (void)gs232b_response_encode(&response, fixture->response[index],
            sizeof(fixture->response[index]), &fixture->response_size[index]);
    }
}

static inline gs232b_error_t control_respond(void *ctx, gs232b_session_key_t session,
                                             const gs232b_response_t *response)
{
    command_fixture_t *fixture = ctx;
    if (!gs232b_session_key_valid(session) || session.session_id > CONTROL_TEST_SESSIONS ||
        !gs232b_session_key_equal(session, fixture->sessions[session.session_id - 1])) {
        return GS232B_ERROR_STALE_SESSION;
    }
    const size_t index = session.session_id - 1;
    ++fixture->async_responses[index];
    if (fixture->respond_error != GS232B_OK) {
        return fixture->respond_error;
    }
    return gs232b_response_encode(response, fixture->response[index],
        sizeof(fixture->response[index]), &fixture->response_size[index]);
}

static inline gs232b_error_t control_position_context(void *ctx,
    gs232b_position_context_t *context)
{
    command_fixture_t *fixture = ctx;
    gs232b_motion_snapshot_t state;
    const gs232b_error_t error = gs232b_motion_snapshot(&fixture->sim.motion, &state);
    if (error != GS232B_OK) {
        return error;
    }
    context->display = fixture->display;
    memcpy(context->axes, state.axes, sizeof(context->axes));
    context->elevation_enabled = fixture->sim.motion.config.axes[GS232B_AXIS_ELEVATION].enabled;
    return GS232B_OK;
}

static inline bool command_fixture_init(command_fixture_t *fixture,
                                         gs232b_time_us_t start_us)
{
    memset(fixture, 0, sizeof(*fixture));
    gs232b_azimuth_display_settings_init(&fixture->display);
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    CHECK(gs232b_motion_sim_init(&fixture->sim, &config, start_us, WAIT_US) == GS232B_OK);
    gs232b_fake_store_init(&fixture->store);
    const gs232b_control_executor_t executor = {
        .ctx = fixture,
        .validate = control_validate,
        .execute = control_execute,
        .cancel = control_cancel,
        .stop = control_stop,
        .tick = control_executor_tick,
        .snapshot = control_activity,
        .position_context = control_position_context,
        .sequence_target = control_sequence_target,
        .fault = control_fault,
        .complete = control_complete,
        .calibration = {.motion = &fixture->sim.motion,
                        .store = gs232b_fake_store_port(&fixture->store)},
        .respond = control_respond
    };
    const gs232b_clock_t clock = {.ctx = fixture, .now_us = control_clock};
    CHECK(gs232b_control_init(&fixture->control, clock, executor,
                              fixture->request_bytes, sizeof(fixture->request_bytes)) ==
          GS232B_OK);
    for (size_t i = 0; i < CONTROL_TEST_SESSIONS; ++i) {
        CHECK(gs232b_control_session_open(&fixture->control, (gs232b_transport_t)i,
            fixture->line_storage[i], sizeof(fixture->line_storage[i]),
            &fixture->sessions[i]) == GS232B_OK);
    }
    return true;
}

static inline gs232b_command_context_t command_context(command_fixture_t *fixture,
                                                       size_t session)
{
    gs232b_command_context_t context = {0};
    gs232b_control_capture(&fixture->control, fixture->sessions[session], &context);
    return context;
}

static inline gs232b_error_t command_feed(command_fixture_t *fixture, size_t session,
                                           const char *bytes)
{
    return gs232b_control_feed(&fixture->control, command_context(fixture, session),
                               (const uint8_t *)bytes, strlen(bytes));
}

static inline bool command_run(command_fixture_t *fixture, size_t session,
                                 const char *line)
{
    CHECK(command_feed(fixture, session, line) == GS232B_OK);
    CHECK(gs232b_control_tick(&fixture->control) == GS232B_OK);
    return true;
}

static inline bool command_advance(command_fixture_t *fixture, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        CHECK(gs232b_motion_sim_step(&fixture->sim, STEP_US) == GS232B_OK);
        CHECK(gs232b_control_tick(&fixture->control) == GS232B_OK);
        CHECK(!fixture->sim.unsafe_output);
    }
    return true;
}

static inline bool command_owner(command_fixture_t *fixture, size_t session)
{
    gs232b_control_snapshot_t state;
    CHECK(gs232b_control_snapshot(&fixture->control, &state) == GS232B_OK);
    CHECK(gs232b_session_key_equal(state.owner, fixture->sessions[session]));
    CHECK(state.owner_transport == (gs232b_transport_t)session);
    return true;
}

static inline bool command_unowned(command_fixture_t *fixture)
{
    gs232b_control_snapshot_t state;
    CHECK(gs232b_control_snapshot(&fixture->control, &state) == GS232B_OK);
    CHECK(state.owner.session_id == 0 && state.owner.generation == 0);
    return true;
}

#endif
