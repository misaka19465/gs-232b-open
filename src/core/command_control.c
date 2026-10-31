#include "command_control_internal.h"

#include <string.h>

bool gs232b_control_valid(const gs232b_control_t *control)
{
    return control != NULL && control->initialized;
}

gs232b_control_session_t *gs232b_control_find(gs232b_control_t *control,
                                            gs232b_session_key_t session)
{
    if (!gs232b_session_key_valid(session) ||
        session.session_id > GS232B_CONTROL_MAX_SESSIONS) {
        return NULL;
    }
    gs232b_control_session_t *slot = &control->sessions[session.session_id - 1];
    return slot->connected && gs232b_session_key_equal(slot->key, session) ?
           slot : NULL;
}

gs232b_error_t gs232b_control_check(gs232b_control_t *control,
                                    gs232b_command_context_t context)
{
    if (!gs232b_control_valid(control) ||
        !gs232b_session_key_valid(context.session) || context.epoch == 0) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_control_session_t *slot = gs232b_control_find(control, context.session);
    if (slot == NULL) {
        return GS232B_ERROR_STALE_SESSION;
    }
    return gs232b_command_context_current(context, slot->key, control->epoch) ?
           GS232B_OK : GS232B_ERROR_CONFLICT;
}

bool gs232b_control_idle(const gs232b_control_activity_t *activity)
{
    return !activity->axes_active[0] && !activity->axes_active[1] &&
           activity->output_stopped &&
           activity->sequence != GS232B_SEQUENCE_LOADED_WAIT_T &&
           activity->sequence != GS232B_SEQUENCE_RUNNING &&
           activity->interaction == GS232B_INTERACTION_NONE;
}

void gs232b_control_complete(gs232b_control_t *control,
                              const gs232b_control_request_t *request,
                              gs232b_error_t error, uint32_t count)
{
    if (control->executor.complete != NULL) {
        control->executor.complete(control->executor.ctx, request, error, count);
    }
}

gs232b_error_t gs232b_control_invalidate(gs232b_control_t *control)
{
    control->request_count = 0;
    control->request_bytes_used = 0;
    for (size_t i = 0; i < GS232B_CONTROL_MAX_SESSIONS; ++i) {
        control->sessions[i].pending_requests = 0;
        gs232b_line_parser_invalidate(&control->sessions[i].parser);
    }
    const gs232b_error_t error = gs232b_command_epoch_advance(&control->epoch);
    if (error != GS232B_OK) {
        control->epoch_exhausted = true;
    }
    return error;
}

gs232b_error_t gs232b_control_stop_now(gs232b_control_t *control,
                                      uint8_t mask, bool revoke_owner)
{
    const gs232b_error_t epoch_error = gs232b_control_invalidate(control);
    control->executor.cancel(control->executor.ctx);
    if (revoke_owner || epoch_error != GS232B_OK) {
        control->owner = (gs232b_session_key_t){0};
    }
    control->idle_timer_running = false;
    /* NOTE: epoch 耗尽不妨碍停止；此时全停并永久拒绝后续动作。 */
    const gs232b_error_t error = control->executor.stop(control->executor.ctx,
        epoch_error == GS232B_OK ? mask : GS232B_CONTROL_ALL_AXES);
    gs232b_control_observe_fault(control);
    return error;
}

gs232b_error_t gs232b_control_observe_fault(gs232b_control_t *control)
{
    gs232b_control_activity_t activity = {0};
    const gs232b_error_t error = control->executor.snapshot(control->executor.ctx,
                                                            &activity);
    if (error != GS232B_OK || activity.faulted) {
        if (!control->fault_seen) {
            gs232b_control_invalidate(control);
            control->executor.cancel(control->executor.ctx);
            control->owner = (gs232b_session_key_t){0};
            control->idle_timer_running = false;
            if (error != GS232B_OK) {
                /* NOTE: 无法证明执行端状态时必须撤销全部动作；已报告的
                 * 运动故障由执行端停机，避免重复尝试失败的输出。 */
                control->executor.stop(control->executor.ctx, GS232B_CONTROL_ALL_AXES);
            }
        }
        control->fault_seen = true;
    } else {
        control->fault_seen = false;
    }
    return error != GS232B_OK ? error :
           activity.faulted ? GS232B_ERROR_HARDWARE_FAULT : GS232B_OK;
}

gs232b_error_t gs232b_control_init(gs232b_control_t *control, gs232b_clock_t clock,
                                  gs232b_control_executor_t executor,
                                  uint8_t *request_bytes, size_t request_capacity)
{
    if (control == NULL || clock.now_us == NULL || request_bytes == NULL ||
        request_capacity < GS232B_GS_LINE_MAX_BYTES || executor.validate == NULL ||
        executor.execute == NULL || executor.cancel == NULL || executor.stop == NULL ||
        executor.tick == NULL || executor.snapshot == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_control_activity_t activity = {0};
    const gs232b_error_t error = executor.snapshot(executor.ctx, &activity);
    if (error != GS232B_OK) {
        return error;
    }
    if (!gs232b_control_idle(&activity)) {
        return GS232B_ERROR_BUSY;
    }
    memset(control, 0, sizeof(*control));
    control->clock = clock;
    control->executor = executor;
    control->request_bytes = request_bytes;
    control->request_capacity = request_capacity;
    control->epoch = 1;
    control->fault_seen = activity.faulted;
    control->initialized = true;
    return GS232B_OK;
}

gs232b_error_t gs232b_control_session_open(gs232b_control_t *control,
                                          gs232b_transport_t transport,
                                          uint8_t *line_storage, size_t capacity,
                                          gs232b_session_key_t *session)
{
    if (session != NULL) {
        *session = (gs232b_session_key_t){0};
    }
    if (!gs232b_control_valid(control) || session == NULL || line_storage == NULL ||
        capacity < GS232B_GS_LINE_MAX_BYTES || transport < GS232B_TRANSPORT_UART1 ||
        transport > GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < GS232B_CONTROL_MAX_SESSIONS; ++i) {
        gs232b_control_session_t *slot = &control->sessions[i];
        if (slot->connected || slot->generation == UINT64_MAX) {
            continue;
        }
        const uint64_t generation = slot->generation + 1;
        memset(slot, 0, sizeof(*slot));
        slot->generation = generation;
        slot->key = (gs232b_session_key_t){(uint32_t)i + 1, generation};
        slot->transport = transport;
        slot->connected = true;
        gs232b_line_parser_init(&slot->parser, line_storage, capacity);
        *session = slot->key;
        return GS232B_OK;
    }
    return GS232B_ERROR_RESOURCE_EXHAUSTED;
}

static void drop_session_requests(gs232b_control_t *control,
                                   gs232b_session_key_t session)
{
    size_t count = 0;
    size_t used = 0;
    for (size_t i = 0; i < control->request_count; ++i) {
        gs232b_control_queued_t request = control->requests[i];
        if (gs232b_session_key_equal(request.context.session, session)) {
            continue;
        }
        memmove(control->request_bytes + used,
                control->request_bytes + request.offset, request.size);
        request.offset = used;
        used += request.size;
        control->requests[count++] = request;
    }
    control->request_count = count;
    control->request_bytes_used = used;
}

gs232b_error_t gs232b_control_session_close(gs232b_control_t *control,
                                           gs232b_session_key_t session)
{
    if (!gs232b_control_valid(control) || !gs232b_session_key_valid(session)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_control_session_t *slot = gs232b_control_find(control, session);
    if (slot == NULL) {
        return GS232B_ERROR_STALE_SESSION;
    }
    slot->connected = false;
    gs232b_error_t error = GS232B_OK;
    if (gs232b_session_key_equal(control->owner, session)) {
        error = gs232b_control_stop_now(control, GS232B_CONTROL_ALL_AXES, true);
    } else {
        drop_session_requests(control, session);
    }
    slot->pending_requests = 0;
    slot->parser = (gs232b_line_parser_t){0};
    /* NOTE: 已接纳的停止不能因来源关闭而撤回；只丢弃其应答地址。 */
    memset(slot->stops, 0, sizeof(slot->stops));
    return error;
}

gs232b_error_t gs232b_control_capture(gs232b_control_t *control,
                                     gs232b_session_key_t session,
                                     gs232b_command_context_t *context)
{
    if (context != NULL) {
        *context = (gs232b_command_context_t){0};
    }
    if (!gs232b_control_valid(control) || context == NULL ||
        !gs232b_session_key_valid(session)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (gs232b_control_find(control, session) == NULL) {
        return GS232B_ERROR_STALE_SESSION;
    }
    *context = (gs232b_command_context_t){session, control->epoch};
    return GS232B_OK;
}

gs232b_error_t gs232b_control_take(gs232b_control_t *control,
                                  gs232b_command_context_t context)
{
    gs232b_error_t error = gs232b_control_check(control, context);
    if (error != GS232B_OK) {
        return error;
    }
    if (control->epoch_exhausted) {
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    if (control->stop_mask != 0) {
        return GS232B_ERROR_BUSY;
    }
    if (gs232b_control_find(control, context.session)->transport ==
        GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS) {
        return GS232B_ERROR_NOT_AUTHORIZED;
    }
    if (gs232b_session_key_valid(control->owner) &&
        !gs232b_session_key_equal(control->owner, context.session)) {
        return GS232B_ERROR_NOT_OWNER;
    }
    gs232b_control_activity_t activity = {0};
    error = control->executor.snapshot(control->executor.ctx, &activity);
    if (error != GS232B_OK || activity.faulted) {
        return error != GS232B_OK ? error : GS232B_ERROR_HARDWARE_FAULT;
    }
    if (!gs232b_session_key_valid(control->owner)) {
        if (!gs232b_control_idle(&activity)) {
            return GS232B_ERROR_BUSY;
        }
        /* NOTE: 自动取权不使同一批新鲜请求失效；撤权已推进 epoch，
         * 不允许从有效旧控制者直接换人，因此每次换人均跨越 epoch。 */
        control->owner = context.session;
    }
    control->idle_timer_running = gs232b_control_idle(&activity);
    control->idle_since_us = control->clock.now_us(control->clock.ctx);
    return GS232B_OK;
}

gs232b_error_t gs232b_control_release(gs232b_control_t *control,
                                     gs232b_command_context_t context)
{
    gs232b_error_t error = gs232b_control_check(control, context);
    if (error != GS232B_OK) {
        return error;
    }
    if (!gs232b_session_key_equal(control->owner, context.session)) {
        return GS232B_ERROR_NOT_OWNER;
    }
    if (control->stop_mask != 0) {
        return GS232B_ERROR_BUSY;
    }
    gs232b_control_activity_t activity = {0};
    error = control->executor.snapshot(control->executor.ctx, &activity);
    if (error != GS232B_OK || !gs232b_control_idle(&activity)) {
        return error != GS232B_OK ? error : GS232B_ERROR_BUSY;
    }
    control->executor.cancel(control->executor.ctx);
    control->owner = (gs232b_session_key_t){0};
    control->idle_timer_running = false;
    return gs232b_control_invalidate(control);
}

gs232b_error_t gs232b_control_request_stop(gs232b_control_t *control,
                                          gs232b_command_context_t context,
                                          gs232b_request_access_t stop)
{
    const gs232b_error_t error = gs232b_control_check(control, context);
    if (error != GS232B_OK) {
        return error;
    }
    if (stop < GS232B_REQUEST_STOP_AZ || stop > GS232B_REQUEST_STOP_ALL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const size_t index = (size_t)(stop - GS232B_REQUEST_STOP_AZ);
    gs232b_control_stop_notice_t *notice =
        &gs232b_control_find(control, context.session)->stops[index];
    control->stop_mask |= stop == GS232B_REQUEST_STOP_AZ ? GS232B_CONTROL_AZ_MASK :
                          stop == GS232B_REQUEST_STOP_EL ? GS232B_CONTROL_EL_MASK :
                                                          GS232B_CONTROL_ALL_AXES;
    control->full_stop_pending |= stop == GS232B_REQUEST_STOP_ALL;
    if (notice->count == UINT32_MAX) {
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    notice->context = context;
    ++notice->count;
    return GS232B_OK;
}

gs232b_error_t gs232b_control_snapshot(const gs232b_control_t *control,
                                      gs232b_control_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!gs232b_control_valid(control)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const gs232b_error_t error = control->executor.snapshot(control->executor.ctx,
                                                            &snapshot->activity);
    if (error != GS232B_OK) {
        return error;
    }
    snapshot->owner = control->owner;
    if (gs232b_session_key_valid(control->owner)) {
        snapshot->owner_transport =
            control->sessions[control->owner.session_id - 1].transport;
    }
    snapshot->epoch = control->epoch;
    snapshot->pending_requests = control->request_count;
    snapshot->request_bytes_used = control->request_bytes_used;
    snapshot->stop_mask = control->stop_mask;
    snapshot->full_stop_pending = control->full_stop_pending;
    snapshot->idle_timer_running = control->idle_timer_running;
    snapshot->idle_since_us = control->idle_since_us;
    snapshot->epoch_exhausted = control->epoch_exhausted;
    return GS232B_OK;
}
