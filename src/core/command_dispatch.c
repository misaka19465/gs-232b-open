#include "command_control_internal.h"

#include <string.h>

static void pop_request(gs232b_control_t *control)
{
    const gs232b_control_queued_t first = control->requests[0];
    if (first.is_position) {
        control->input_reservation = (gs232b_command_context_t){0};
        control->upload_queued = false;
    }
    gs232b_control_session_t *slot = gs232b_control_find(control, first.context.session);
    if (slot != NULL) {
        --slot->pending_requests;
    }
    control->request_bytes_used -= first.size;
    memmove(control->request_bytes, control->request_bytes + first.size,
            control->request_bytes_used);
    --control->request_count;
    for (size_t i = 0; i < control->request_count; ++i) {
        control->requests[i] = control->requests[i + 1];
        control->requests[i].offset -= first.size;
    }
}

static gs232b_error_t execute_request(gs232b_control_t *control,
                                       gs232b_control_request_t *request,
                                       gs232b_response_t *response)
{
    if (request->access == GS232B_REQUEST_OWNER_TAKE) {
        return gs232b_control_take(control, request->context);
    }
    if (request->access == GS232B_REQUEST_OWNER_RELEASE) {
        return gs232b_control_release(control, request->context);
    }
    if (request->access == GS232B_REQUEST_OWNER_GET) {
        return GS232B_OK;
    }
    const bool bare = request->position != NULL &&
                      request->position->format == GS232B_POSITION_BARE;
    const bool calibrating = gs232b_control_calibration_handles(control, request);
    gs232b_error_t error = calibrating ?
        gs232b_control_calibration_validate(control, request) : bare ? GS232B_OK :
        control->executor.validate(control->executor.ctx, request);
    if (error != GS232B_OK) {
        return error;
    }
    const bool writing = request->access == GS232B_REQUEST_WRITE;
    const bool acquiring = writing && !gs232b_session_key_valid(control->owner);
    if (writing) {
        if (control->epoch_exhausted) {
            return GS232B_ERROR_RESOURCE_EXHAUSTED;
        }
        if (!acquiring &&
            !gs232b_session_key_equal(control->owner, request->context.session)) {
            return GS232B_ERROR_NOT_OWNER;
        }
        error = gs232b_control_check_reservation(control, request->context.session);
        if (error != GS232B_OK) {
            return error;
        }
    }
    gs232b_position_context_t position_context;
    gs232b_position_plan_t position_plan;
    if (request->position != NULL) {
        error = gs232b_control_validate_position(control, request->position,
                                                  &position_context, &position_plan);
        if (error != GS232B_OK) {
            return error;
        }
    }
    bool display_changed = false;
    error = gs232b_control_sequence_display_change(control, request, &display_changed);
    if (error != GS232B_OK) {
        return error;
    }
    if (writing) {
        if (acquiring) {
            error = gs232b_control_take(control, request->context);
            if (error != GS232B_OK) {
                return error;
            }
        }
    }
    if (calibrating) {
        error = gs232b_control_calibration_execute(control, request, response);
        if (error == GS232B_OK) {
            request->response = response;
        }
    } else if (bare) {
        gs232b_control_cancel(control);
        error = control->executor.stop(control->executor.ctx, GS232B_CONTROL_ALL_AXES);
        if (error == GS232B_OK) {
            error = GS232B_ERROR_INVALID_ARGUMENT;
        }
    } else if (request->kind == GS232B_LINE_GS && request->position == NULL &&
               gs232b_byte_span_equal_ascii_ci(request->line, "T")) {
        error = gs232b_control_sequence_start(control);
    } else {
        gs232b_control_request_t prepared = *request;
        prepared.position_plan = request->position != NULL ? &position_plan : NULL;
        error = control->executor.execute(control->executor.ctx, &prepared);
        if (error == GS232B_OK && request->position != NULL) {
            gs232b_sequence_store_commit(&control->sequence, request->position,
                                          &position_context, &position_plan);
            gs232b_control_sequence_loaded(control);
        } else if (error == GS232B_OK && request->kind == GS232B_LINE_GS &&
                   (display_changed || gs232b_byte_span_equal_ascii_ci(request->line, "R") ||
                    gs232b_byte_span_equal_ascii_ci(request->line, "L") ||
                    gs232b_byte_span_equal_ascii_ci(request->line, "U") ||
                    gs232b_byte_span_equal_ascii_ci(request->line, "D"))) {
            gs232b_control_sequence_clear(control);
        }
    }
    const gs232b_error_t state_error = gs232b_control_observe_fault(control);
    if (error == GS232B_OK && request->access != GS232B_REQUEST_QUERY) {
        error = state_error;
    }
    if (writing && gs232b_session_key_equal(control->owner, request->context.session)) {
        gs232b_control_activity_t activity = {0};
        if (gs232b_control_activity(control, &activity) == GS232B_OK) {
            if (acquiring && error != GS232B_OK &&
                gs232b_control_idle(&activity, control->sequence_state.state)) {
                control->owner = (gs232b_session_key_t){0};
                control->idle_timer_running = false;
                gs232b_control_invalidate(control);
            } else if (error == GS232B_OK) {
                control->idle_timer_running =
                    gs232b_control_idle(&activity, control->sequence_state.state);
                control->idle_since_us = control->clock.now_us(control->clock.ctx);
            }
        }
    }
    return error;
}

gs232b_error_t gs232b_control_dispatch_one(gs232b_control_t *control)
{
    if (control->request_count == 0) {
        return GS232B_OK;
    }
    const gs232b_control_queued_t queued = control->requests[0];
    const gs232b_command_epoch_t dispatch_epoch = control->epoch;
    gs232b_control_request_t request = {
        .context = queued.context,
        .kind = queued.kind,
        .access = queued.access,
        .line = {control->request_bytes + queued.offset, queued.size},
        .position = queued.is_position ? &queued.position : NULL
    };
    gs232b_error_t error = gs232b_control_check(control, request.context);
    if (error == GS232B_OK) {
        /* NOTE: N/空行在排队后可能由查询变为校准输入，必须按执行瞬间
         * 的发起会话和交互状态重新分类，不能用旧的只读权限取消交互。 */
        if (request.position == NULL && request.kind == GS232B_LINE_GS &&
            (request.line.size == 0 ||
             gs232b_byte_span_equal_ascii_ci(request.line, "N"))) {
            gs232b_control_activity_t activity = {0};
            error = gs232b_control_activity(control, &activity);
            request.access = gs232b_session_key_equal(control->owner,
                                                       request.context.session) &&
                             activity.interaction != GS232B_INTERACTION_NONE ?
                             GS232B_REQUEST_WRITE : GS232B_REQUEST_QUERY;
        }
        gs232b_response_t response;
        if (error == GS232B_OK) {
            error = execute_request(control, &request, &response);
        }
        gs232b_control_complete(control, &request, error, 1);
    }
    /* NOTE: RELEASE/故障可能已清整队列；此时不能再弹出新的队头。 */
    if (control->request_count != 0 && control->epoch == dispatch_epoch) {
        pop_request(control);
    }
    return error;
}

static gs232b_error_t process_stops(gs232b_control_t *control)
{
    const uint8_t mask = control->stop_mask;
    const bool full_stop = control->full_stop_pending;
    control->stop_mask = 0;
    control->full_stop_pending = false;
    const gs232b_error_t error = gs232b_control_stop_now(control, mask, full_stop);
    for (size_t i = 0; i < GS232B_CONTROL_MAX_SESSIONS; ++i) {
        gs232b_control_session_t *slot = &control->sessions[i];
        for (size_t j = 0; j < 3; ++j) {
            const gs232b_control_stop_notice_t notice = slot->stops[j];
            slot->stops[j].count = 0;
            if (notice.count != 0 && gs232b_control_find(control, notice.context.session)) {
                const gs232b_control_request_t request = {
                    .context = notice.context,
                    .kind = GS232B_LINE_GS,
                    .access = (gs232b_request_access_t)(GS232B_REQUEST_STOP_AZ + j)
                };
                gs232b_control_complete(control, &request, error, notice.count);
            }
        }
    }
    return error;
}

static gs232b_error_t release_if_idle(gs232b_control_t *control)
{
    if (!gs232b_session_key_valid(control->owner)) {
        control->idle_timer_running = false;
        return GS232B_OK;
    }
    if (gs232b_session_key_valid(control->input_reservation.session)) {
        return GS232B_OK;
    }
    gs232b_control_activity_t activity = {0};
    const gs232b_error_t error = gs232b_control_activity(control, &activity);
    if (error != GS232B_OK) {
        return error;
    }
    if (!gs232b_control_idle(&activity, control->sequence_state.state)) {
        control->idle_timer_running = false;
        return GS232B_OK;
    }
    const gs232b_time_us_t now_us = control->clock.now_us(control->clock.ctx);
    if (!control->idle_timer_running || now_us < control->idle_since_us) {
        control->idle_since_us = now_us;
        control->idle_timer_running = true;
    } else if (now_us - control->idle_since_us >= GS232B_CONTROL_IDLE_RELEASE_US) {
        /* NOTE: 自动释放不删除 COMPLETED 的末点序号和已加载数据。 */
        control->executor.cancel(control->executor.ctx);
        control->owner = (gs232b_session_key_t){0};
        control->idle_timer_running = false;
        return gs232b_control_invalidate(control);
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_control_tick(gs232b_control_t *control)
{
    if (!gs232b_control_valid(control)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const bool stopped = control->stop_mask != 0;
    const gs232b_error_t stop_error = stopped ? process_stops(control) : GS232B_OK;
    const gs232b_error_t schedule_error = gs232b_control_sequence_check(control);
    const gs232b_error_t tick_error = control->executor.tick(control->executor.ctx);
    const gs232b_error_t calibration_error = gs232b_control_calibration_tick(control);
    const gs232b_error_t state_error = gs232b_control_observe_fault(control);
    const gs232b_error_t sequence_error = schedule_error == GS232B_OK &&
        tick_error == GS232B_OK && state_error == GS232B_OK ?
        gs232b_control_sequence_tick(control) : GS232B_OK;
    gs232b_control_expire_lines(control);
    const gs232b_error_t command_error = stopped ? GS232B_OK :
                                                 gs232b_control_dispatch_one(control);
    const gs232b_error_t idle_error = release_if_idle(control);
    return stop_error != GS232B_OK ? stop_error :
           schedule_error != GS232B_OK ? schedule_error :
           tick_error != GS232B_OK ? tick_error :
           state_error != GS232B_OK ? state_error :
           sequence_error != GS232B_OK ? sequence_error :
           calibration_error != GS232B_OK ? calibration_error :
           command_error != GS232B_OK ? command_error : idle_error;
}
