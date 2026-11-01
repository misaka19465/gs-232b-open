#include "gs_dispatcher_internal.h"

#include <string.h>

static void cancel(void *ctx)
{
    /* NOTE: 本执行端没有额外动作队列；序列及交互由控制核心统一撤销。 */
    (void)ctx;
}

static gs232b_error_t stop(void *ctx, uint8_t mask)
{
    return gs232b_motion_stop(((gs232b_dispatcher_t *)ctx)->motion, mask);
}

static gs232b_error_t tick(void *ctx)
{
    return gs232b_motion_tick(((gs232b_dispatcher_t *)ctx)->motion);
}

static gs232b_error_t activity(void *ctx, gs232b_control_activity_t *activity)
{
    gs232b_motion_snapshot_t state;
    const gs232b_error_t error = gs232b_motion_snapshot(((gs232b_dispatcher_t *)ctx)->motion,
                                                        &state);
    if (error != GS232B_OK) {
        return error;
    }
    *activity = (gs232b_control_activity_t){0};
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        const gs232b_axis_mode_t mode = state.axes[axis].mode;
        activity->axes_active[axis] = mode == GS232B_AXIS_MANUAL ||
            mode == GS232B_AXIS_GOTO || mode == GS232B_AXIS_REVERSAL_WAIT;
    }
    activity->output_stopped = state.output.known &&
        state.output.last_successful.azimuth == GS232B_AZ_STOP &&
        state.output.last_successful.elevation == GS232B_EL_STOP &&
        state.output.last_successful.speed_permille == 0;
    activity->faulted = state.fault.code != GS232B_FAULT_NONE;
    return GS232B_OK;
}

static gs232b_error_t position_context(void *ctx, gs232b_position_context_t *context)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    gs232b_motion_snapshot_t state;
    const gs232b_error_t error = gs232b_motion_snapshot(dispatcher->motion, &state);
    if (error == GS232B_OK) {
        context->display = dispatcher->display;
        memcpy(context->axes, state.axes, sizeof(context->axes));
        context->elevation_enabled = dispatcher->motion->config.axes[GS232B_AXIS_ELEVATION].enabled;
    }
    return error;
}

static gs232b_error_t sequence_target(void *ctx, const gs232b_position_plan_t *plan)
{
    return gs232b_motion_goto(((gs232b_dispatcher_t *)ctx)->motion,
        plan->axis_mask, plan->targets_mdeg);
}

static gs232b_error_t fault(void *ctx, gs232b_fault_code_t code, gs232b_error_t error)
{
    return gs232b_motion_abort(((gs232b_dispatcher_t *)ctx)->motion, code, error);
}

gs232b_error_t gs232b_dispatcher_init(gs232b_dispatcher_t *dispatcher,
    gs232b_motion_t *motion, gs232b_store_t store, gs232b_session_io_t io,
    uint8_t *request_bytes, size_t capacity)
{
    if (dispatcher == NULL || motion == NULL || !motion->initialized ||
        io.send == NULL || io.close == NULL || store.load == NULL || store.save == NULL ||
        request_bytes == NULL || capacity < GS232B_LINE_STORAGE_BYTES) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(dispatcher, 0, sizeof(*dispatcher));
    dispatcher->motion = motion;
    dispatcher->store = store;
    dispatcher->io = io;
    gs232b_error_t error = gs232b_persistence_display_load(&store, &dispatcher->display,
                                                           &dispatcher->display_revision);
    if (error == GS232B_ERROR_NOT_FOUND) {
        gs232b_azimuth_display_settings_init(&dispatcher->display);
    } else if (error != GS232B_OK) {
        return error;
    }
    const gs232b_control_executor_t executor = {
        .ctx = dispatcher, .validate = gs232b_gs_validate, .execute = gs232b_gs_execute,
        .cancel = cancel, .stop = stop, .tick = tick, .snapshot = activity,
        .position_context = position_context, .sequence_target = sequence_target,
        .fault = fault, .complete = gs232b_gs_complete,
        .calibration = {.motion = motion, .store = store}, .respond = gs232b_gs_respond,
        .submitted = gs232b_gs_submitted, .discarded = gs232b_gs_discarded
    };
    error = gs232b_control_init(&dispatcher->control, motion->manager->clock, executor,
                                request_bytes, capacity);
    dispatcher->initialized = error == GS232B_OK;
    return error;
}

gs232b_error_t gs232b_dispatcher_session_open(gs232b_dispatcher_t *dispatcher,
    gs232b_transport_t transport, uint8_t *line_storage, size_t capacity,
    gs232b_session_key_t *session)
{
    if (dispatcher == NULL || !dispatcher->initialized) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_error_t error = gs232b_control_session_open(&dispatcher->control, transport,
        line_storage, capacity, session);
    if (error != GS232B_OK) {
        return error;
    }
    dispatcher->sessions[session->session_id - 1U] =
        (gs232b_dispatcher_session_t){.key = *session};
    static const uint8_t banner[] = "Connect OK\r\n";
    error = gs232b_session_send(&dispatcher->io, *session, banner, sizeof(banner) - 1U);
    if (error != GS232B_OK) {
        gs232b_dispatcher_session_close(dispatcher, *session);
        *session = (gs232b_session_key_t){0};
    }
    return error;
}

gs232b_error_t gs232b_dispatcher_session_close(gs232b_dispatcher_t *dispatcher,
    gs232b_session_key_t session)
{
    if (dispatcher == NULL || !dispatcher->initialized ||
        !gs232b_session_key_valid(session) || session.session_id > GS232B_CONTROL_MAX_SESSIONS) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_dispatcher_session_t *slot = &dispatcher->sessions[session.session_id - 1U];
    if (!gs232b_session_key_equal(slot->key, session)) {
        return GS232B_ERROR_STALE_SESSION;
    }
    const gs232b_error_t control_error = gs232b_control_session_close(&dispatcher->control, session);
    const gs232b_error_t io_error = gs232b_session_close(&dispatcher->io, session);
    *slot = (gs232b_dispatcher_session_t){0};
    if (gs232b_session_key_equal(dispatcher->watchdog_session, session)) {
        dispatcher->watchdog_session = (gs232b_session_key_t){0};
        dispatcher->watchdog_timeout_us = 0;
        dispatcher->watchdog_renewed_us = 0;
    }
    /* NOTE: 异步校准发送失败可能已关闭核心会话，仍须清理实际连接。 */
    return control_error != GS232B_OK && control_error != GS232B_ERROR_STALE_SESSION ?
        control_error : io_error;
}

gs232b_error_t gs232b_dispatcher_capture(gs232b_dispatcher_t *dispatcher,
    gs232b_session_key_t session, gs232b_command_context_t *context)
{
    return dispatcher == NULL || !dispatcher->initialized ? GS232B_ERROR_INVALID_ARGUMENT :
        gs232b_control_capture(&dispatcher->control, session, context);
}

gs232b_error_t gs232b_dispatcher_feed(gs232b_dispatcher_t *dispatcher,
    gs232b_command_context_t context, const uint8_t *bytes, size_t size)
{
    if (dispatcher == NULL || !dispatcher->initialized) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const gs232b_error_t error = gs232b_control_feed(&dispatcher->control, context, bytes, size);
    const gs232b_error_t send_error = gs232b_gs_flush(dispatcher);
    return error != GS232B_OK ? error : send_error;
}

gs232b_error_t gs232b_dispatcher_resync_input(gs232b_dispatcher_t *dispatcher,
    gs232b_session_key_t session)
{
    return dispatcher == NULL || !dispatcher->initialized ? GS232B_ERROR_INVALID_ARGUMENT :
        gs232b_control_resync_input(&dispatcher->control, session);
}

gs232b_error_t gs232b_dispatcher_input_lost(gs232b_dispatcher_t *dispatcher,
    gs232b_session_key_t session)
{
    if (dispatcher == NULL || !dispatcher->initialized) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const gs232b_error_t error = gs232b_control_input_lost(&dispatcher->control, session);
    const gs232b_error_t send_error = gs232b_gs_flush(dispatcher);
    return error != GS232B_OK ? error : send_error;
}

gs232b_error_t gs232b_dispatcher_tick(gs232b_dispatcher_t *dispatcher)
{
    if (dispatcher == NULL || !dispatcher->initialized) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (dispatcher->watchdog_timeout_us != 0) {
        if (!gs232b_session_key_equal(dispatcher->watchdog_session, dispatcher->control.owner)) {
            dispatcher->watchdog_timeout_us = 0;
        } else {
            const gs232b_time_us_t now = dispatcher->control.clock.now_us(dispatcher->control.clock.ctx);
            if (now < dispatcher->watchdog_renewed_us ||
                now - dispatcher->watchdog_renewed_us >= dispatcher->watchdog_timeout_us) {
                dispatcher->watchdog_timeout_us = 0;
                const gs232b_error_t error = gs232b_dispatcher_session_close(dispatcher,
                    dispatcher->watchdog_session);
                return error == GS232B_OK ? GS232B_ERROR_TIMEOUT : error;
            }
        }
    }
    const gs232b_error_t error = gs232b_control_tick(&dispatcher->control);
    const gs232b_error_t send_error = gs232b_gs_flush(dispatcher);
    return error != GS232B_OK ? error : send_error;
}

gs232b_error_t gs232b_dispatcher_watchdog_ping(gs232b_dispatcher_t *dispatcher,
    gs232b_session_key_t session)
{
    if (dispatcher == NULL || !dispatcher->initialized) { return GS232B_ERROR_INVALID_ARGUMENT; }
    gs232b_command_context_t context;
    const gs232b_error_t error = gs232b_dispatcher_capture(dispatcher, session, &context);
    if (error != GS232B_OK) { return error; }
    if (!gs232b_session_key_equal(session, dispatcher->control.owner)) {
        return GS232B_ERROR_NOT_OWNER;
    }
    const gs232b_time_us_t now = dispatcher->control.clock.now_us(dispatcher->control.clock.ctx);
    if (dispatcher->watchdog_timeout_us != 0 &&
        gs232b_session_key_equal(dispatcher->watchdog_session, session) &&
        (now < dispatcher->watchdog_renewed_us ||
         now - dispatcher->watchdog_renewed_us >= dispatcher->watchdog_timeout_us)) {
        const gs232b_error_t closed = gs232b_dispatcher_session_close(dispatcher, session);
        return closed == GS232B_OK ? GS232B_ERROR_TIMEOUT : closed;
    }
    dispatcher->watchdog_renewed_us = now;
    return GS232B_OK;
}

gs232b_error_t gs232b_dispatcher_watchdog_set(gs232b_dispatcher_t *dispatcher,
    gs232b_session_key_t session, uint32_t timeout_ms)
{
    if (timeout_ms != 0 && (timeout_ms < 1000 || timeout_ms > 600000)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const gs232b_error_t error = gs232b_dispatcher_watchdog_ping(dispatcher, session);
    if (error == GS232B_OK) {
        dispatcher->watchdog_session = session;
        dispatcher->watchdog_timeout_us = (gs232b_time_us_t)timeout_ms * 1000U;
    }
    return error;
}
