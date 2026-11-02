#include "gs_dispatcher_internal.h"
#include "management_crypto.h"

#include <stdio.h>
#include <string.h>

static const gs232b_json_field_t auth_fields[] = {
    {.name = "token", .type = GS232B_JSON_STRING, .required = true, .max_bytes = 64}
};
static const gs232b_json_schema_t auth_schema = {auth_fields, 1};

gs232b_error_t gs232b_management_access(gs232b_dispatcher_t *dispatcher,
    const gs232b_extension_request_t *request)
{
    static const char *const allowed[] = {
        "SYS INFO", "SESSION AUTH", "NET GET", "NET SET", "CFG BEGIN", "CFG CHECK",
        "CFG STATUS", "CFG APPLY", "CFG CONFIRM", "CFG ABORT", "PROV STATUS",
        "PROV SCAN", "PROV STOP"
    };
    if (request->transport != GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS) return GS232B_OK;
    gs232b_provisioning_status_t status;
    if (gs232b_provisioning_snapshot(&dispatcher->provisioning, &status) != GS232B_OK ||
        status.state != GS232B_PROVISIONING_ACTIVE || status.remaining_us == 0) {
        return GS232B_ERROR_NOT_AUTHORIZED;
    }
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        if (strcmp(request->cmd, allowed[i]) == 0) return GS232B_OK;
    }
    return GS232B_ERROR_NOT_AUTHORIZED;
}

gs232b_error_t gs232b_management_network_authorize(gs232b_dispatcher_t *dispatcher,
    const gs232b_extension_request_t *request)
{
    const gs232b_error_t access = gs232b_management_access(dispatcher, request);
    if (access != GS232B_OK) return access;
    if (request->transport == GS232B_TRANSPORT_UART1 ||
        request->transport == GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS) return GS232B_OK;
    const gs232b_dispatcher_session_t *slot =
        &dispatcher->sessions[request->context.session.session_id - 1U];
    return dispatcher->admin_enabled && slot->management_authorized ?
        GS232B_OK : GS232B_ERROR_NOT_AUTHORIZED;
}

static void revoke(gs232b_dispatcher_t *dispatcher)
{
    for (size_t i = 0; i < GS232B_CONTROL_MAX_SESSIONS; ++i) {
        gs232b_dispatcher_session_t *slot = &dispatcher->sessions[i];
        if (slot->management_authorized) {
            gs232b_config_transaction_close(&dispatcher->network_config, slot->key);
        }
        slot->management_authorized = false;
    }
}

static gs232b_error_t check(void *ctx, const gs232b_extension_request_t *request)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    if (strcmp(request->cmd, "SESSION AUTH") == 0) {
        if (request->transport != GS232B_TRANSPORT_ETHERNET_WS &&
            request->transport != GS232B_TRANSPORT_WIFI_WS) return GS232B_ERROR_NOT_AUTHORIZED;
        return dispatcher->admin_enabled ? GS232B_OK : GS232B_ERROR_NOT_AUTHORIZED;
    }
    return request->transport == GS232B_TRANSPORT_UART1 ? GS232B_OK : GS232B_ERROR_NOT_AUTHORIZED;
}

static gs232b_error_t handle(void *ctx, const gs232b_extension_request_t *request,
    uint8_t *data, size_t capacity, size_t *size)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    if (strcmp(request->cmd, "ADMIN DISABLE") == 0) {
        const char response[] = "{\"enabled\":false}";
        if (capacity < sizeof(response) - 1U) return GS232B_ERROR_BUFFER_TOO_SMALL;
        const gs232b_error_t error = gs232b_store_erase(&dispatcher->store, GS232B_STORE_ADMIN);
        if (error != GS232B_OK) return error;
        revoke(dispatcher);
        dispatcher->admin_enabled = false;
        dispatcher->admin_revision = 0;
        gs232b_management_clear(dispatcher->admin_verifier, sizeof(dispatcher->admin_verifier));
        memcpy(data, response, sizeof(response) - 1U);
        *size = sizeof(response) - 1U;
        return GS232B_OK;
    }
    uint8_t digest[32];
    char token[65] = {0};
    gs232b_error_t error;
    if (strcmp(request->cmd, "SESSION AUTH") == 0) {
        gs232b_json_value_t value;
        size_t length = 0;
        error = gs232b_json_get(request->arguments, "token", &value);
        if (error == GS232B_OK) error = gs232b_json_string_decode(value,
            (uint8_t *)token, 64, &length);
        if (error == GS232B_OK) error = gs232b_management_digest((uint8_t *)token, length, digest);
        gs232b_management_clear(token, sizeof(token));
        unsigned difference = (unsigned)(length ^ 64U);
        if (error == GS232B_OK) {
            for (size_t i = 0; i < sizeof(digest); ++i) difference |= digest[i] ^ dispatcher->admin_verifier[i];
        }
        gs232b_management_clear(digest, sizeof(digest));
        gs232b_dispatcher_session_t *slot =
            &dispatcher->sessions[request->context.session.session_id - 1U];
        if (error != GS232B_OK || difference != 0) {
            slot->management_authorized = false;
            gs232b_config_transaction_close(&dispatcher->network_config, slot->key);
            return error != GS232B_OK ? error : GS232B_ERROR_NOT_AUTHORIZED;
        }
        const char response[] = "{\"authenticated\":true}";
        if (capacity < sizeof(response) - 1U) return GS232B_ERROR_BUFFER_TOO_SMALL;
        slot->management_authorized = true;
        memcpy(data, response, sizeof(response) - 1U);
        *size = sizeof(response) - 1U;
        return GS232B_OK;
    }
    /* NOTE: token 只进入本次 UART 响应；持久记录和会话只保存校验材料/身份位。 */
    if (capacity < 92U) return GS232B_ERROR_BUFFER_TOO_SMALL;
    error = gs232b_management_token(token);
    if (error == GS232B_OK) error = gs232b_management_digest((uint8_t *)token, 64, digest);
    uint64_t revision = 0;
    if (error == GS232B_OK) error = gs232b_persistence_commit(&dispatcher->store,
        GS232B_STORE_ADMIN, dispatcher->admin_revision, GS232B_PERSISTENCE_SCHEMA_ADMIN,
        digest, sizeof(digest), &revision);
    if (error == GS232B_OK) {
        revoke(dispatcher);
        memcpy(dispatcher->admin_verifier, digest, sizeof(digest));
        dispatcher->admin_revision = revision;
        dispatcher->admin_enabled = true;
        const int n = snprintf((char *)data, capacity, "{\"enabled\":true,\"token\":\"%s\"}", token);
        if (n < 0 || (size_t)n >= capacity) error = GS232B_ERROR_BUFFER_TOO_SMALL;
        else *size = (size_t)n;
    }
    gs232b_management_clear(token, sizeof(token));
    gs232b_management_clear(digest, sizeof(digest));
    return error;
}

/** @brief 注册管理认证命令，并从独立域恢复 SHA-256 校验材料。 */
gs232b_error_t gs232b_admin_register(gs232b_dispatcher_t *dispatcher)
{
    gs232b_persisted_record_t record;
    gs232b_error_t error = gs232b_persistence_load_schema(&dispatcher->store,
        GS232B_STORE_ADMIN, GS232B_PERSISTENCE_SCHEMA_ADMIN, &record);
    if (error == GS232B_OK) {
        if (record.payload_size != sizeof(dispatcher->admin_verifier)) return GS232B_ERROR_CORRUPT;
        memcpy(dispatcher->admin_verifier, record.payload, sizeof(dispatcher->admin_verifier));
        dispatcher->admin_revision = record.revision;
        dispatcher->admin_enabled = true;
    } else if (error != GS232B_ERROR_NOT_FOUND) return error;
    const gs232b_extension_handler_t handlers[] = {
        {.domain = "ADMIN", .action = "ENABLE", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "ADMIN", .action = "DISABLE", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "SESSION", .action = "AUTH", .schema = &auth_schema,
         .check = check, .handle = handle, .ctx = dispatcher}
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        error = gs232b_extension_register(&dispatcher->extensions, &handlers[i]);
        if (error != GS232B_OK) return error;
    }
    return GS232B_OK;
}
