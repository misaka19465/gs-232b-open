#include "gs_dispatcher_internal.h"
#include "network_config_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool read_only(const gs232b_extension_request_t *request)
{
    return strcmp(request->cmd, "NET GET") == 0 || strcmp(request->cmd, "CFG STATUS") == 0;
}

static gs232b_error_t check(void *ctx, const gs232b_extension_request_t *request)
{
    (void)ctx;
    if (read_only(request)) return GS232B_OK;
    /* NOTE: ETH/STA 管理认证尚未启用；运动所有权不能代替网络管理权限。 */
    return request->transport == GS232B_TRANSPORT_UART1 ||
        request->transport == GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS ?
        GS232B_OK : GS232B_ERROR_NOT_AUTHORIZED;
}

static gs232b_error_t handle(void *ctx, const gs232b_extension_request_t *request,
    uint8_t *data, size_t capacity, size_t *size)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    gs232b_config_transaction_t *transaction = &dispatcher->network_config;
    if (strcmp(request->cmd, "NET GET") == 0) {
        return gs232b_net_config_encode(transaction, &dispatcher->network_status, data, capacity, size);
    }
    if (strcmp(request->cmd, "CFG STATUS") == 0) {
        return gs232b_config_status_encode(transaction, data, capacity, size);
    }
    /* NOTE: 先在副本上完成操作和响应编码；容量不足不能留下部分事务副作用。 */
    gs232b_config_transaction_t next = *transaction;
    gs232b_error_t error;
    int n = 0;
    if (strcmp(request->cmd, "CFG BEGIN") == 0) {
        error = gs232b_config_transaction_begin(&next, request->context.session);
        if (error == GS232B_OK) n = snprintf((char *)data, capacity,
            "{\"candidate_id\":%" PRIu32 ",\"base_revision\":%" PRIu64 ",\"idle_timeout_s\":60}",
            next.candidate_id, next.base_revision);
    } else if (strcmp(request->cmd, "NET SET") == 0) {
        error = gs232b_config_transaction_access(&next, request->context.session);
        gs232b_net_config_t candidate;
        if (error == GS232B_OK) error = gs232b_net_config_patch(&next.candidate, request->arguments, &candidate);
        if (error == GS232B_OK) error = gs232b_config_transaction_update(&next, request->context.session, &candidate);
        if (error == GS232B_OK) n = snprintf((char *)data, capacity,
            "{\"candidate_id\":%" PRIu32 "}", next.candidate_id);
    } else if (strcmp(request->cmd, "CFG CHECK") == 0) {
        gs232b_net_changes_t changes;
        error = gs232b_config_transaction_check(&next, request->context.session, &changes);
        if (error == GS232B_OK) n = snprintf((char *)data, capacity,
            "{\"candidate_id\":%" PRIu32 ",\"valid\":true,\"changes\":{\"eth\":%s,\"wifi\":%s}}",
            next.candidate_id, changes.eth ? "true" : "false", changes.wifi ? "true" : "false");
    } else {
        error = gs232b_config_transaction_abort(&next, request->context.session);
        if (error == GS232B_OK) n = snprintf((char *)data, capacity, "{}");
    }
    if (error != GS232B_OK) return error;
    if (n < 0 || (size_t)n >= capacity) return GS232B_ERROR_BUFFER_TOO_SMALL;
    *size = (size_t)n;
    *transaction = next;
    return GS232B_OK;
}

gs232b_error_t gs232b_network_register(gs232b_dispatcher_t *dispatcher)
{
    const gs232b_extension_handler_t handlers[] = {
        {.domain = "CFG", .action = "BEGIN", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "CFG", .action = "CHECK", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "CFG", .action = "STATUS", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "CFG", .action = "ABORT", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "NET", .action = "GET", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "NET", .action = "SET", .schema = &gs232b_net_patch_schema,
         .check = check, .handle = handle, .ctx = dispatcher}
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        const gs232b_error_t error = gs232b_extension_register(&dispatcher->extensions, &handlers[i]);
        if (error != GS232B_OK) return error;
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_dispatcher_network_configure(gs232b_dispatcher_t *dispatcher,
    const gs232b_net_config_t *active, uint64_t revision, gs232b_net_status_port_t status)
{
    if (dispatcher == NULL || !dispatcher->initialized) return GS232B_ERROR_INVALID_ARGUMENT;
    if (dispatcher->control.dispatching) return GS232B_ERROR_BUSY;
    for (size_t i = 0; i < GS232B_CONTROL_MAX_SESSIONS; ++i) {
        if (dispatcher->control.sessions[i].generation != 0) return GS232B_ERROR_BUSY;
    }
    const gs232b_error_t error = gs232b_config_transaction_init(&dispatcher->network_config,
        dispatcher->control.clock, active, revision);
    if (error == GS232B_OK) dispatcher->network_status = status;
    return error;
}
