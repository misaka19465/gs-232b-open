#include "gs_dispatcher_internal.h"
#include "command_control_internal.h"
#include "network_config_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const gs232b_json_field_t apply_fields[] = {
    {.name = "candidate_id", .type = GS232B_JSON_NUMBER, .required = true},
    {.name = "revision", .type = GS232B_JSON_NUMBER},
    {.name = "timeout_s", .type = GS232B_JSON_NUMBER, .required = true}
};
static const gs232b_json_field_t confirm_fields[] = {
    {.name = "trial_id", .type = GS232B_JSON_NUMBER, .required = true}
};
static const gs232b_json_field_t abort_fields[] = {
    {.name = "trial_id", .type = GS232B_JSON_NUMBER}
};
static const gs232b_json_schema_t apply_schema = {
    apply_fields, sizeof(apply_fields) / sizeof(apply_fields[0])
};
static const gs232b_json_schema_t confirm_schema = {
    confirm_fields, sizeof(confirm_fields) / sizeof(confirm_fields[0])
};
static const gs232b_json_schema_t abort_schema = {
    abort_fields, sizeof(abort_fields) / sizeof(abort_fields[0])
};

static bool read_only(const gs232b_extension_request_t *request)
{
    return strcmp(request->cmd, "NET GET") == 0 || strcmp(request->cmd, "CFG STATUS") == 0;
}

static gs232b_error_t check(void *ctx, const gs232b_extension_request_t *request)
{
    (void)ctx;
    if (read_only(request)) return GS232B_OK;
    return request->transport == GS232B_TRANSPORT_UART1 ||
        request->transport == GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS ?
        GS232B_OK : GS232B_ERROR_NOT_AUTHORIZED;
}

static gs232b_error_t uint_field(gs232b_json_value_t object, const char *name,
    uint32_t *number)
{
    gs232b_json_value_t value;
    const gs232b_error_t error = gs232b_json_get(object, name, &value);
    return error == GS232B_OK ? gs232b_json_uint32(value, number) : error;
}

static gs232b_error_t apply_allowed(gs232b_dispatcher_t *dispatcher)
{
    gs232b_control_t *control = &dispatcher->control;
    if (control->request_count > 1U || control->stop_mask != 0 || control->full_stop_pending ||
        gs232b_session_key_valid(control->input_reservation.session) ||
        control->sequence_state.state != GS232B_SEQUENCE_EMPTY ||
        control->sequence.active.point_count != 0 ||
        control->calibration.kind != GS232B_INTERACTION_NONE) return GS232B_ERROR_BUSY;
    gs232b_control_activity_t activity;
    const gs232b_error_t error = gs232b_control_activity(control, &activity);
    if (error != GS232B_OK) return error;
    if (activity.faulted) return GS232B_ERROR_HARDWARE_FAULT;
    if (!activity.output_stopped || activity.axes_active[GS232B_AXIS_AZIMUTH] ||
        activity.axes_active[GS232B_AXIS_ELEVATION] ||
        activity.interaction != GS232B_INTERACTION_NONE) return GS232B_ERROR_BUSY;
    return GS232B_OK;
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
    gs232b_config_transaction_t next = *transaction;
    gs232b_error_t error = GS232B_OK;
    int n = 0;
    char prepared[192];
    if (strcmp(request->cmd, "CFG BEGIN") == 0) {
        error = gs232b_config_transaction_begin(&next, request->context.session);
        if (error == GS232B_OK) n = snprintf(prepared, sizeof(prepared),
            "{\"candidate_id\":%" PRIu32 ",\"base_revision\":%" PRIu64
            ",\"idle_timeout_s\":60}", next.candidate_id, next.base_revision);
    } else if (strcmp(request->cmd, "NET SET") == 0) {
        error = gs232b_config_transaction_access(&next, request->context.session);
        gs232b_net_config_t candidate;
        if (error == GS232B_OK) error = gs232b_net_config_patch(&next.candidate,
            request->arguments, &candidate);
        if (error == GS232B_OK) error = gs232b_config_transaction_update(&next,
            request->context.session, &candidate);
        if (error == GS232B_OK) n = snprintf(prepared, sizeof(prepared),
            "{\"candidate_id\":%" PRIu32 "}", next.candidate_id);
    } else if (strcmp(request->cmd, "CFG CHECK") == 0) {
        gs232b_net_changes_t changes;
        error = gs232b_config_transaction_check(&next, request->context.session, &changes);
        if (error == GS232B_OK) n = snprintf(prepared, sizeof(prepared),
            "{\"candidate_id\":%" PRIu32 ",\"valid\":true,\"changes\":{\"eth\":%s,\"wifi\":%s}}",
            next.candidate_id, changes.eth ? "true" : "false", changes.wifi ? "true" : "false");
    } else if (strcmp(request->cmd, "CFG APPLY") == 0) {
        uint32_t candidate_id = 0, timeout_s = 0;
        error = uint_field(request->arguments, "candidate_id", &candidate_id);
        gs232b_json_value_t revision_value;
        if (error == GS232B_OK && gs232b_json_get(request->arguments, "revision",
            &revision_value) == GS232B_OK) {
            uint64_t revision = 0;
            error = gs232b_json_uint64(revision_value, &revision);
            if (error == GS232B_OK && revision != next.base_revision) error = GS232B_ERROR_CONFLICT;
        }
        if (error == GS232B_OK) error = uint_field(request->arguments, "timeout_s", &timeout_s);
        if (error == GS232B_OK) error = apply_allowed(dispatcher);
        if (error == GS232B_OK && dispatcher->network_apply.apply == NULL) error = GS232B_ERROR_NOT_READY;
        if (error == GS232B_OK && next.last_trial_id == UINT32_MAX) error = GS232B_ERROR_RESOURCE_EXHAUSTED;
        const uint32_t expected_id = next.last_trial_id + 1U;
        if (error == GS232B_OK) {
            n = snprintf(prepared, sizeof(prepared), "{\"trial_id\":%" PRIu32
                ",\"state\":\"active\",\"timeout_s\":%" PRIu32 "}",
                expected_id, timeout_s);
            if (n < 0 || (size_t)n >= sizeof(prepared) || (size_t)n > capacity) {
                error = GS232B_ERROR_BUFFER_TOO_SMALL;
            }
        }
        if (error == GS232B_OK) {
            uint32_t trial_id = 0;
            error = gs232b_config_trial_apply(&next, request->context.session,
                candidate_id, timeout_s, &dispatcher->store, dispatcher->network_apply,
                &trial_id);
            if (trial_id != 0) {
                n = snprintf(prepared, sizeof(prepared), "{\"trial_id\":%" PRIu32
                    ",\"state\":\"active\",\"timeout_s\":%" PRIu32 "}",
                    trial_id, timeout_s);
            }
            *transaction = next;
        }
        if (error == GS232B_OK) {
            memcpy(data, prepared, (size_t)n);
            *size = (size_t)n;
            return GS232B_OK;
        }
        return error;
    } else if (strcmp(request->cmd, "CFG CONFIRM") == 0) {
        uint32_t trial_id = 0;
        error = uint_field(request->arguments, "trial_id", &trial_id);
        if (error == GS232B_OK) {
            const uint64_t response_revision = next.active_revision +
                (next.trial.state == GS232B_CONFIG_TRIAL_ACTIVE &&
                 next.trial.id == trial_id && next.active_revision != UINT64_MAX ? 1U : 0U);
            n = snprintf(prepared, sizeof(prepared),
                "{\"trial_id\":%" PRIu32 ",\"confirmed\":true,\"active_revision\":%" PRIu64 "}",
                trial_id, response_revision);
            if (n < 0 || (size_t)n >= sizeof(prepared) || (size_t)n > capacity) {
                return GS232B_ERROR_BUFFER_TOO_SMALL;
            }
        }
        if (error == GS232B_OK) error = gs232b_config_trial_confirm(&next, trial_id,
            &dispatcher->store, dispatcher->network_apply);
        *transaction = next;
        if (error != GS232B_OK) return error;
    } else if (strcmp(request->cmd, "CFG ABORT") == 0) {
        n = snprintf(prepared, sizeof(prepared), "{}");
        if (n < 0 || (size_t)n > capacity) return GS232B_ERROR_BUFFER_TOO_SMALL;
        uint32_t trial_id = 0;
        gs232b_json_value_t value;
        const gs232b_error_t found = gs232b_json_get(request->arguments, "trial_id", &value);
        if (found == GS232B_OK) {
            error = gs232b_json_uint32(value, &trial_id);
            if (error == GS232B_OK) error = gs232b_config_trial_abort(&next, trial_id,
                dispatcher->network_apply);
        } else {
            error = gs232b_config_transaction_abort(&next, request->context.session);
        }
        *transaction = next;
        if (error != GS232B_OK) return error;
    }
    if (error != GS232B_OK) return error;
    if (n < 0 || (size_t)n >= sizeof(prepared) || (size_t)n > capacity) {
        return GS232B_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(data, prepared, (size_t)n);
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
        {.domain = "CFG", .action = "APPLY", .schema = &apply_schema,
         .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "CFG", .action = "CONFIRM", .schema = &confirm_schema,
         .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "CFG", .action = "ABORT", .schema = &abort_schema,
         .check = check, .handle = handle, .ctx = dispatcher},
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
    if (dispatcher->network_config.trial.state != GS232B_CONFIG_TRIAL_NONE) return GS232B_ERROR_BUSY;
    for (size_t i = 0; i < GS232B_CONTROL_MAX_SESSIONS; ++i) {
        if (dispatcher->control.sessions[i].generation != 0) return GS232B_ERROR_BUSY;
    }
    gs232b_error_t error = gs232b_config_transaction_init(&dispatcher->network_config,
        dispatcher->control.clock, active, revision);
    if (error == GS232B_OK) {
        uint32_t last_trial_id = 0;
        error = gs232b_persistence_trial_id_load(&dispatcher->store, &last_trial_id);
        if (error == GS232B_OK) dispatcher->network_config.last_trial_id = last_trial_id;
        gs232b_network_config_t persisted;
        uint64_t persisted_revision = 0;
        const gs232b_error_t load_error = gs232b_persistence_network_load(
            &dispatcher->store, &persisted, &persisted_revision);
        if (error == GS232B_OK && load_error == GS232B_OK) {
            if (persisted_revision != revision) error = GS232B_ERROR_CONFLICT;
            else dispatcher->network_config.last_confirmed_trial_id =
                persisted.last_confirmed_trial_id;
        } else if (error == GS232B_OK && load_error != GS232B_ERROR_NOT_FOUND) {
            error = load_error;
        }
        if (error == GS232B_OK) dispatcher->network_status = status;
    }
    return error;
}

gs232b_error_t gs232b_dispatcher_network_apply_configure(gs232b_dispatcher_t *dispatcher,
    gs232b_net_apply_port_t apply)
{
    if (dispatcher == NULL || !dispatcher->initialized || apply.apply == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (dispatcher->control.dispatching) return GS232B_ERROR_BUSY;
    for (size_t i = 0; i < GS232B_CONTROL_MAX_SESSIONS; ++i) {
        if (dispatcher->control.sessions[i].generation != 0) return GS232B_ERROR_BUSY;
    }
    dispatcher->network_apply = apply;
    return GS232B_OK;
}
