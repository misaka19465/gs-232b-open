#include "gs_dispatcher_internal.h"

static gs232b_error_t parse(gs232b_dispatcher_t *dispatcher,
    const gs232b_control_request_t *request, gs232b_extension_request_t *extension)
{
    gs232b_error_t error = gs232b_extension_parse(request->line.bytes,
        request->line.size, extension);
    if (error != GS232B_OK) return error;
    if (!gs232b_session_key_valid(request->context.session) ||
        request->context.session.session_id > GS232B_CONTROL_MAX_SESSIONS) return GS232B_ERROR_STALE_SESSION;
    const gs232b_control_session_t *slot =
        &dispatcher->control.sessions[request->context.session.session_id - 1U];
    if (!slot->connected || !gs232b_session_key_equal(slot->key, request->context.session)) return GS232B_ERROR_STALE_SESSION;
    extension->context = request->context;
    extension->transport = slot->transport;
    extension->management_only = slot->management_only;
    return gs232b_management_access(dispatcher, extension);
}

gs232b_error_t gs232b_extension_validate_request(void *ctx,
    const gs232b_control_request_t *request)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    gs232b_extension_request_t extension;
    gs232b_error_t error = parse(dispatcher, request, &extension);
    return error != GS232B_OK ? error :
        gs232b_extension_validate(&dispatcher->extensions, &extension);
}

gs232b_error_t gs232b_extension_execute_request(void *ctx,
    const gs232b_control_request_t *request, gs232b_response_t *response)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    gs232b_extension_request_t extension;
    gs232b_error_t error = parse(dispatcher, request, &extension);
    if (error != GS232B_OK) return error;
    gs232b_dispatcher_session_t *slot =
        &dispatcher->sessions[request->context.session.session_id - 1U];
    if (request->reply_id == 0 || slot->failure != GS232B_OK) return GS232B_ERROR_RESOURCE_EXHAUSTED;
    /* NOTE: 每会话仅借用一份正文，发送前不能由后续 handler 覆盖；先检查再产生副作用。 */
    if (slot->extension_reply_id != 0) return GS232B_ERROR_BUSY;
    response->kind = GS232B_RESPONSE_EXTENSION;
    error = gs232b_extension_dispatch(&dispatcher->extensions, &extension,
        slot->extension_data, sizeof(slot->extension_data), &response->data.extension);
    if (error == GS232B_OK) slot->extension_reply_id = request->reply_id;
    return error;
}

gs232b_error_t gs232b_dispatcher_register_extension(gs232b_dispatcher_t *dispatcher,
    const gs232b_extension_handler_t *handler)
{
    if (dispatcher == NULL || !dispatcher->initialized) return GS232B_ERROR_INVALID_ARGUMENT;
    if (dispatcher->control.dispatching) return GS232B_ERROR_BUSY;
    return gs232b_extension_register(&dispatcher->extensions, handler);
}
