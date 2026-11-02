#include "gs_dispatcher_internal.h"
#include "command_control_internal.h"

#include <stdio.h>
#include <string.h>

static const gs232b_json_field_t watchdog_fields[] = {
    {.name = "timeout_ms", .type = GS232B_JSON_NUMBER, .required = true}
};
static const gs232b_json_schema_t watchdog_schema = {watchdog_fields, 1};
static const gs232b_json_field_t reset_fields[] = {
    {.name = "confirm", .type = GS232B_JSON_STRING, .required = true, .max_bytes = 12}
};
static const gs232b_json_schema_t reset_schema = {reset_fields, 1};

static gs232b_error_t idle(gs232b_dispatcher_t *dispatcher, bool clearing_fault)
{
    const gs232b_control_t *control = &dispatcher->control;
    gs232b_control_activity_t activity;
    const gs232b_error_t error = gs232b_control_activity(control, &activity);
    if (error != GS232B_OK) return error;
    if (activity.axes_active[0] || activity.axes_active[1] ||
        activity.interaction != GS232B_INTERACTION_NONE ||
        (!activity.output_stopped && !(clearing_fault && activity.faulted)) ||
        control->stop_mask != 0 || control->full_stop_pending ||
        control->sequence_state.state == GS232B_SEQUENCE_LOADED_WAIT_T ||
        control->sequence_state.state == GS232B_SEQUENCE_RUNNING ||
        gs232b_session_key_valid(control->input_reservation.session)) return GS232B_ERROR_BUSY;
    return GS232B_OK;
}

static gs232b_error_t check(void *ctx, const gs232b_extension_request_t *request)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    const char *cmd = request->cmd;
    if (strcmp(cmd, "SYS INFO") == 0 || strcmp(cmd, "OWNER GET") == 0 ||
        strcmp(cmd, "MOTION STATUS") == 0) return GS232B_OK;
    if (strcmp(cmd, "NET RESET") == 0) {
        if (request->transport != GS232B_TRANSPORT_UART1) return GS232B_ERROR_NOT_AUTHORIZED;
        gs232b_json_value_t value;
        uint8_t confirmation[12]; size_t length = 0;
        if (gs232b_json_get(request->arguments, "confirm", &value) != GS232B_OK ||
            gs232b_json_string_decode(value, confirmation, sizeof(confirmation), &length) != GS232B_OK ||
            length != 12 || memcmp(confirmation, "NETWORK_ONLY", 12) != 0) return GS232B_ERROR_INVALID_ARGUMENT;
        if (gs232b_config_transaction_live(&dispatcher->network_config) ||
            dispatcher->network_config.trial.state != GS232B_CONFIG_TRIAL_NONE ||
            dispatcher->network_reset_pending) return GS232B_ERROR_CONFLICT;
        if (dispatcher->control.request_count > (dispatcher->control.dispatching ? 1U : 0U) ||
            (gs232b_session_key_valid(dispatcher->control.owner) &&
             !gs232b_session_key_equal(dispatcher->control.owner, request->context.session))) return GS232B_ERROR_BUSY;
        if (dispatcher->network_apply.apply == NULL || dispatcher->store.erase == NULL) return GS232B_ERROR_NOT_READY;
        return idle(dispatcher, false);
    }
    if (strcmp(cmd, "FAULT CLEAR") == 0) {
        if (request->transport == GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS) return GS232B_ERROR_NOT_AUTHORIZED;
        if (gs232b_session_key_valid(dispatcher->control.owner) &&
            !gs232b_session_key_equal(dispatcher->control.owner, request->context.session)) return GS232B_ERROR_NOT_OWNER;
        return idle(dispatcher, true);
    }
    const gs232b_control_session_t *slot =
        &dispatcher->control.sessions[request->context.session.session_id - 1U];
    if (slot->transport == GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS) {
        return GS232B_ERROR_NOT_AUTHORIZED;
    }
    if (strcmp(cmd, "SESSION WATCHDOG") == 0) {
        gs232b_json_value_t value; uint32_t timeout = 0;
        if (gs232b_json_get(request->arguments, "timeout_ms", &value) != GS232B_OK ||
            gs232b_json_uint32(value, &timeout) != GS232B_OK ||
            (timeout != 0 && (timeout < 1000 || timeout > 600000))) return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (strcmp(cmd, "OWNER TAKE") == 0) return GS232B_OK;
    return gs232b_session_key_equal(dispatcher->control.owner, request->context.session) ?
        GS232B_OK : GS232B_ERROR_NOT_OWNER;
}

gs232b_error_t gs232b_management_reset_tick(gs232b_dispatcher_t *dispatcher)
{
    if (!dispatcher->network_reset_pending) return GS232B_OK;
    gs232b_error_t error = dispatcher->network_apply.apply(dispatcher->network_apply.ctx,
        &dispatcher->network_config.active);
    if (error != GS232B_OK && error != GS232B_ERROR_WOULD_BLOCK) return error;
    error = gs232b_provisioning_stop(&dispatcher->provisioning);
    if (error != GS232B_OK) return error;
    error = gs232b_provisioning_start(&dispatcher->provisioning, 0);
    if (error == GS232B_OK) dispatcher->network_reset_pending = false;
    return error;
}

static gs232b_error_t handle(void *ctx, const gs232b_extension_request_t *request,
    uint8_t *data, size_t capacity, size_t *size)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    const char *cmd = request->cmd;
    gs232b_error_t error = GS232B_OK;
    /* NOTE: 有副作用的管理处理器使用 dispatcher 的完整 2KiB 正文槽，先保证响应资源。 */
    if (strcmp(cmd, "SYS INFO") != 0 && strcmp(cmd, "OWNER GET") != 0 &&
        strcmp(cmd, "MOTION STATUS") != 0 && capacity < GS232B_EXTENSION_DATA_MAX_BYTES) {
        return GS232B_ERROR_BUFFER_TOO_SMALL;
    }
    /* NOTE: 内置处理器在控制任务内调用核心取/撤权；外部 handler 仍禁止重入。 */
    if (strcmp(cmd, "OWNER TAKE") == 0) error = gs232b_control_take(&dispatcher->control, request->context);
    else if (strcmp(cmd, "OWNER RELEASE") == 0) error = gs232b_control_release(&dispatcher->control, request->context);
    else if (strcmp(cmd, "SESSION WATCHDOG") == 0) {
        gs232b_json_value_t value; uint32_t timeout = 0;
        (void)gs232b_json_get(request->arguments, "timeout_ms", &value);
        (void)gs232b_json_uint32(value, &timeout);
        error = gs232b_dispatcher_watchdog_set(dispatcher, request->context.session, timeout);
    } else if (strcmp(cmd, "SESSION PING") == 0) {
        error = gs232b_dispatcher_watchdog_ping(dispatcher, request->context.session);
    } else if (strcmp(cmd, "FAULT CLEAR") == 0) {
        error = gs232b_motion_clear_fault(dispatcher->motion);
        if (error == GS232B_OK) {
            gs232b_control_cancel(&dispatcher->control);
            error = gs232b_control_invalidate(&dispatcher->control);
            dispatcher->control.fault_seen = false;
        } else dispatcher->control.fault_seen = true;
    } else if (strcmp(cmd, "NET RESET") == 0) {
        error = gs232b_store_erase(&dispatcher->store, GS232B_STORE_NETWORK);
        if (error != GS232B_OK) return error;
        const uint32_t last_id = dispatcher->network_config.last_id;
        const uint32_t last_trial_id = dispatcher->network_config.last_trial_id;
        (void)gs232b_config_transaction_init(&dispatcher->network_config, dispatcher->control.clock, NULL, 0);
        dispatcher->network_config.last_id = last_id;
        dispatcher->network_config.last_trial_id = last_trial_id;
        dispatcher->provisioning.active = dispatcher->network_config.active;
        dispatcher->provisioning.configured = false;
        dispatcher->provisioning.status.first_boot = true;
        dispatcher->network_reset_pending = true;
        error = gs232b_control_invalidate(&dispatcher->control);
        if (error == GS232B_OK) error = gs232b_management_reset_tick(dispatcher);
    }
    if (error != GS232B_OK) return error;
    return gs232b_management_status(dispatcher, cmd, data, capacity, size);
}

gs232b_error_t gs232b_management_register(gs232b_dispatcher_t *dispatcher)
{
    gs232b_error_t error = gs232b_admin_register(dispatcher);
    if (error != GS232B_OK) return error;
    const gs232b_extension_handler_t handlers[] = {
        {.domain = "SYS", .action = "INFO", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "OWNER", .action = "GET", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "OWNER", .action = "TAKE", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "OWNER", .action = "RELEASE", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "SESSION", .action = "WATCHDOG", .schema = &watchdog_schema,
         .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "SESSION", .action = "PING", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "MOTION", .action = "STATUS", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "FAULT", .action = "CLEAR", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "NET", .action = "RESET", .schema = &reset_schema,
         .check = check, .handle = handle, .ctx = dispatcher}
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        error = gs232b_extension_register(&dispatcher->extensions, &handlers[i]);
        if (error != GS232B_OK) return error;
    }
    return GS232B_OK;
}
