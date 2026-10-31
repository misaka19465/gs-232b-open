#include "command_control_internal.h"

#include <string.h>

static bool equal(gs232b_byte_span_t line, const char *text)
{
    return gs232b_byte_span_equal_ascii_ci(line, text);
}

gs232b_error_t gs232b_control_classify(gs232b_line_kind_t kind,
                                      gs232b_byte_span_t line,
                                      gs232b_request_access_t *access)
{
    if (kind == GS232B_LINE_GS) {
        if (equal(line, "S")) {
            *access = GS232B_REQUEST_STOP_ALL;
        } else if (equal(line, "A")) {
            *access = GS232B_REQUEST_STOP_AZ;
        } else if (equal(line, "E")) {
            *access = GS232B_REQUEST_STOP_EL;
        } else if (line.size == 0 || equal(line, "C") || equal(line, "B") ||
                   equal(line, "C2") || equal(line, "N") || equal(line, "H") ||
                   equal(line, "H2") || equal(line, "H3")) {
            *access = GS232B_REQUEST_QUERY;
        } else {
            *access = GS232B_REQUEST_WRITE;
        }
        return GS232B_OK;
    }

    gs232b_byte_span_t command, action, extra;
    if (!gs232b_byte_span_next_space_token(&line, &command) ||
        !gs232b_byte_span_next_space_token(&line, &action) ||
        gs232b_byte_span_next_space_token(&line, &extra)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (equal(command, "!OWNER")) {
        if (equal(action, "GET")) {
            *access = GS232B_REQUEST_OWNER_GET;
        } else if (equal(action, "TAKE")) {
            *access = GS232B_REQUEST_OWNER_TAKE;
        } else if (equal(action, "RELEASE")) {
            *access = GS232B_REQUEST_OWNER_RELEASE;
        } else {
            return GS232B_ERROR_INVALID_ARGUMENT;
        }
        return GS232B_OK;
    }
    if ((equal(command, "!MOTION") && equal(action, "STATUS")) ||
        (equal(command, "!SYS") && equal(action, "INFO")) ||
        (equal(command, "!NET") && equal(action, "GET")) ||
        (equal(command, "!CFG") && equal(action, "STATUS")) ||
        (equal(command, "!PROV") && equal(action, "STATUS"))) {
        *access = GS232B_REQUEST_QUERY;
        return GS232B_OK;
    }
    /* NOTE: 未实现的扩展不能默认为只读，避免后续配置写绕过权限检查。 */
    return GS232B_ERROR_NOT_FOUND;
}

gs232b_error_t gs232b_control_enqueue(gs232b_control_t *control,
                                     const gs232b_control_request_t *request)
{
    gs232b_control_session_t *slot = gs232b_control_find(control,
                                                        request->context.session);
    if (slot->pending_requests >= GS232B_CONTROL_REQUESTS_PER_SESSION ||
        control->request_count >= GS232B_CONTROL_MAX_REQUESTS ||
        request->line.size > control->request_capacity - control->request_bytes_used) {
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    gs232b_control_queued_t *queued = &control->requests[control->request_count++];
    *queued = (gs232b_control_queued_t){
        .context = request->context,
        .kind = request->kind,
        .access = request->access,
        .offset = control->request_bytes_used,
        .size = request->line.size,
        .is_position = request->position != NULL
    };
    if (request->position != NULL) {
        queued->position = *request->position;
        control->upload_queued = true;
    } else if (queued->size != 0) {
        memcpy(control->request_bytes + queued->offset, request->line.bytes, queued->size);
    }
    control->request_bytes_used += queued->size;
    ++slot->pending_requests;
    return GS232B_OK;
}

typedef struct {
    gs232b_control_t *control;
    gs232b_control_session_t *session;
    gs232b_error_t error;
} input_context_t;

static gs232b_error_t admit_line(gs232b_control_t *control,
                                  gs232b_control_request_t *request)
{
    gs232b_error_t error = gs232b_control_check(control, request->context);
    if (error != GS232B_OK) {
        return error;
    }
    if (request->position != NULL) {
        request->access = GS232B_REQUEST_WRITE;
    } else {
        error = gs232b_control_classify(request->kind, request->line, &request->access);
    }
    if (error != GS232B_OK) {
        return error;
    }
    if (request->access >= GS232B_REQUEST_STOP_AZ) {
        return gs232b_control_request_stop(control, request->context, request->access);
    }
    if (request->position == NULL && request->kind == GS232B_LINE_GS &&
        gs232b_session_key_equal(control->owner, request->context.session) &&
        (request->line.size == 0 || equal(request->line, "N"))) {
        gs232b_control_activity_t activity = {0};
        error = control->executor.snapshot(control->executor.ctx, &activity);
        if (error != GS232B_OK) {
            return error;
        }
        if (activity.interaction != GS232B_INTERACTION_NONE) {
            request->access = GS232B_REQUEST_WRITE;
        }
    }
    if (request->access == GS232B_REQUEST_WRITE ||
        request->access == GS232B_REQUEST_OWNER_TAKE) {
        if (control->epoch_exhausted) {
            return GS232B_ERROR_RESOURCE_EXHAUSTED;
        }
        if (gs232b_control_find(control, request->context.session)->transport ==
            GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS) {
            return GS232B_ERROR_NOT_AUTHORIZED;
        }
        if (gs232b_session_key_valid(control->owner) &&
            !gs232b_session_key_equal(control->owner, request->context.session)) {
            return GS232B_ERROR_NOT_OWNER;
        }
        error = gs232b_control_check_reservation(control, request->context.session);
        if (error != GS232B_OK) {
            return error;
        }
    }
    if (request->position != NULL) {
        gs232b_position_context_t context;
        gs232b_position_plan_t plan;
        error = gs232b_control_validate_position(control, request->position, &context, &plan);
        if (error != GS232B_OK) {
            return error;
        }
        if (request->position->format == GS232B_POSITION_BARE) {
            return gs232b_control_enqueue(control, request);
        }
    }
    if (request->access == GS232B_REQUEST_WRITE ||
        request->access == GS232B_REQUEST_QUERY) {
        error = control->executor.validate(control->executor.ctx, request);
        if (error != GS232B_OK) {
            return error;
        }
    }
    return gs232b_control_enqueue(control, request);
}

static void on_line(void *ctx, const gs232b_line_event_t *event)
{
    input_context_t *input = ctx;
    gs232b_control_request_t request = {
        .context = input->session->partial_context,
        .kind = event->kind,
        .line = {event->bytes, event->size},
        .position = event->position
    };
    gs232b_error_t error;
    if (event->type == GS232B_LINE_EVENT_ERROR) {
        error = event->error == GS232B_LINE_ERROR_UPLOAD_REJECTED ? event->rejection :
                event->error == GS232B_LINE_ERROR_TIMEOUT ? GS232B_ERROR_TIMEOUT :
                event->error == GS232B_LINE_ERROR_TOO_LONG ?
                    GS232B_ERROR_RESOURCE_EXHAUSTED : GS232B_ERROR_INVALID_ARGUMENT;
    } else {
        error = admit_line(input->control, &request);
    }
    if (error != GS232B_OK) {
        if (event->position != NULL) {
            gs232b_control_upload_abort(input->session);
        }
        if (input->error == GS232B_OK) {
            input->error = error;
        }
        gs232b_control_complete(input->control, &request, error, 1);
    }
}

gs232b_error_t gs232b_control_feed(gs232b_control_t *control,
                                  gs232b_command_context_t context,
                                  const uint8_t *bytes, size_t size)
{
    if (size > GS232B_CONTROL_FEED_MAX_BYTES || (size != 0 && bytes == NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const gs232b_error_t error = gs232b_control_check(control, context);
    if (error != GS232B_OK && error != GS232B_ERROR_CONFLICT) {
        return error;
    }
    gs232b_control_session_t *slot = gs232b_control_find(control, context.session);
    input_context_t input = {control, slot, error};
    const gs232b_time_us_t now_us = control->clock.now_us(control->clock.ctx);
    for (size_t i = 0; i < size; ++i) {
        if (!slot->parser.active) {
            slot->partial_context = context;
        }
        if (error == GS232B_ERROR_CONFLICT) {
            /* NOTE: 不能重盖章旧 RX 块；即使它在停止前尚未进入行解析器，
             * 也必须丢弃，并在跨块半行终止前拒绝新的后缀。 */
            if (!(slot->parser.swallow_lf && bytes[i] == '\n')) {
                slot->parser.active = true;
            }
            gs232b_line_parser_invalidate(&slot->parser);
        }
        gs232b_line_parser_feed(&slot->parser, bytes + i, 1, now_us, on_line, &input);
    }
    return input.error;
}

void gs232b_control_expire_lines(gs232b_control_t *control)
{
    const gs232b_time_us_t now_us = control->clock.now_us(control->clock.ctx);
    for (size_t i = 0; i < GS232B_CONTROL_MAX_SESSIONS; ++i) {
        gs232b_control_session_t *slot = &control->sessions[i];
        if (slot->connected) {
            input_context_t input = {control, slot, GS232B_OK};
            gs232b_line_parser_tick(&slot->parser, now_us, on_line, &input);
        }
    }
}
