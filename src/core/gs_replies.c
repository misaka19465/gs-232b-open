#include "gs_dispatcher_internal.h"

static gs232b_dispatcher_session_t *session_slot(gs232b_dispatcher_t *dispatcher,
                                                  gs232b_session_key_t session)
{
    if (!gs232b_session_key_valid(session) || session.session_id > GS232B_CONTROL_MAX_SESSIONS) {
        return NULL;
    }
    gs232b_dispatcher_session_t *slot = &dispatcher->sessions[session.session_id - 1U];
    return gs232b_session_key_equal(slot->key, session) ? slot : NULL;
}

static gs232b_dispatcher_reply_t *reply_at(gs232b_dispatcher_session_t *slot, size_t index)
{
    return &slot->replies[(slot->head + index) % GS232B_DISPATCHER_REPLIES_PER_SESSION];
}

static gs232b_dispatcher_reply_t *reserve(gs232b_dispatcher_session_t *slot)
{
    if (slot->count == GS232B_DISPATCHER_REPLIES_PER_SESSION || slot->next_id == UINT64_MAX) {
        slot->failure = GS232B_ERROR_RESOURCE_EXHAUSTED;
        return NULL;
    }
    gs232b_dispatcher_reply_t *reply = reply_at(slot, slot->count++);
    *reply = (gs232b_dispatcher_reply_t){.id = ++slot->next_id};
    return reply;
}

static gs232b_dispatcher_reply_t *find_reply(gs232b_dispatcher_session_t *slot, uint64_t id)
{
    if (slot != NULL && id != 0) {
        for (size_t i = 0; i < slot->count; ++i) {
            gs232b_dispatcher_reply_t *reply = reply_at(slot, i);
            if (reply->id == id) {
                return reply;
            }
        }
    }
    return NULL;
}

void gs232b_gs_submitted(void *ctx, gs232b_control_request_t *request)
{
    gs232b_dispatcher_session_t *slot = session_slot(ctx, request->context.session);
    if (slot == NULL) {
        return;
    }
    gs232b_dispatcher_reply_t *reply = reserve(slot);
    if (reply != NULL) {
        request->reply_id = reply->id;
        reply->command = (uint8_t)gs232b_gs_command(request);
    }
}

void gs232b_gs_complete(void *ctx, const gs232b_control_request_t *request,
                         gs232b_error_t result, uint32_t count)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    gs232b_dispatcher_session_t *slot = session_slot(dispatcher, request->context.session);
    if (slot == NULL) {
        return;
    }
    if (request->reply_id == 0 && request->access >= GS232B_REQUEST_STOP_AZ) {
        const gs_command_t stop = request->access == GS232B_REQUEST_STOP_AZ ? GS_COMMAND_A :
            request->access == GS232B_REQUEST_STOP_EL ? GS_COMMAND_E : GS_COMMAND_S;
        for (size_t i = 0; i < slot->count && count != 0; ++i) {
            gs232b_dispatcher_reply_t *reply = reply_at(slot, i);
            if (reply->state == GS_REPLY_WAITING && reply->command == stop) {
                reply->response.kind = result == GS232B_OK ?
                    GS232B_RESPONSE_SUCCESS : GS232B_RESPONSE_ERROR;
                reply->state = GS_REPLY_READY;
                --count;
            }
        }
        return;
    }
    gs232b_dispatcher_reply_t *reply = find_reply(slot, request->reply_id);
    if (reply == NULL) {
        return;
    }
    reply->response = (gs232b_response_t){.kind = GS232B_RESPONSE_SUCCESS};
    if (result == GS232B_OK && request->response != NULL) {
        reply->response = *request->response;
    } else if (result == GS232B_OK && request->access == GS232B_REQUEST_QUERY) {
        result = gs232b_gs_query(dispatcher, (gs_command_t)reply->command, &reply->response);
    }
    if (result != GS232B_OK) {
        reply->response.kind = GS232B_RESPONSE_ERROR;
    }
    reply->state = GS_REPLY_READY;
}

void gs232b_gs_discarded(void *ctx, const gs232b_control_request_t *request)
{
    gs232b_dispatcher_session_t *slot = session_slot(ctx, request->context.session);
    gs232b_dispatcher_reply_t *reply = find_reply(slot, request->reply_id);
    if (reply != NULL) {
        /* NOTE: 仅保留明确只读的 GS 查询；N/空行若已归类为校准输入则静默作废。
         * 查询在停机完成后构造，不在停止动作之前执行普通请求。 */
        reply->state = request->access == GS232B_REQUEST_QUERY &&
            request->kind == GS232B_LINE_GS ? GS_REPLY_QUERY : GS_REPLY_SKIP;
    }
}

gs232b_error_t gs232b_gs_respond(void *ctx, gs232b_session_key_t session,
                                const gs232b_response_t *response)
{
    gs232b_dispatcher_session_t *slot = session_slot(ctx, session);
    if (slot == NULL) {
        return GS232B_ERROR_STALE_SESSION;
    }
    gs232b_dispatcher_reply_t *reply = reserve(slot);
    if (reply == NULL) {
        return slot->failure;
    }
    reply->response = *response;
    reply->state = GS_REPLY_READY;
    return GS232B_OK;
}

gs232b_error_t gs232b_gs_flush(gs232b_dispatcher_t *dispatcher)
{
    gs232b_error_t first_error = GS232B_OK;
    for (size_t i = 0; i < GS232B_CONTROL_MAX_SESSIONS; ++i) {
        gs232b_dispatcher_session_t *slot = &dispatcher->sessions[i];
        if (!gs232b_session_key_valid(slot->key)) {
            continue;
        }
        while (slot->failure == GS232B_OK && slot->count != 0) {
            gs232b_dispatcher_reply_t *reply = reply_at(slot, 0);
            if (reply->state == GS_REPLY_WAITING) {
                break;
            }
            if (reply->state == GS_REPLY_QUERY) {
                if (gs232b_gs_query(dispatcher, (gs_command_t)reply->command,
                                     &reply->response) != GS232B_OK) {
                    reply->response.kind = GS232B_RESPONSE_ERROR;
                }
            }
            if (reply->state != GS_REPLY_SKIP) {
                uint8_t bytes[512];
                size_t size;
                gs232b_error_t error = gs232b_response_encode(&reply->response,
                    bytes, sizeof(bytes), &size);
                if (error == GS232B_OK) {
                    error = gs232b_session_send(&dispatcher->io, slot->key, bytes, size);
                }
                if (error != GS232B_OK) {
                    slot->failure = error;
                    break;
                }
            }
            slot->head = (slot->head + 1U) % GS232B_DISPATCHER_REPLIES_PER_SESSION;
            --slot->count;
        }
        if (slot->failure != GS232B_OK) {
            if (first_error == GS232B_OK) {
                first_error = slot->failure;
            }
            /* NOTE: 回调内只登记失败；到此已离开控制核心，才可关闭并全停。 */
            gs232b_dispatcher_session_close(dispatcher, slot->key);
        }
    }
    return first_error;
}
