/*
 * GS-232B Open - rotator controller firmware.
 * Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

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
static const gs232b_json_field_t prov_start_fields[] = {
    {.name = "timeout_s", .type = GS232B_JSON_NUMBER}
};
static const gs232b_json_schema_t prov_start_schema = {
    prov_start_fields, sizeof(prov_start_fields) / sizeof(prov_start_fields[0])
};
static const gs232b_json_field_t prov_scan_fields[] = {
    {.name = "scan_id", .type = GS232B_JSON_NUMBER},
    {.name = "cursor", .type = GS232B_JSON_NUMBER}
};
static const gs232b_json_schema_t prov_scan_schema = {
    prov_scan_fields, sizeof(prov_scan_fields) / sizeof(prov_scan_fields[0])
};

static bool read_only(const gs232b_extension_request_t *request)
{
    return strcmp(request->cmd, "NET GET") == 0 || strcmp(request->cmd, "CFG STATUS") == 0 ||
        strcmp(request->cmd, "PROV STATUS") == 0;
}

static gs232b_error_t check(void *ctx, const gs232b_extension_request_t *request)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    if (read_only(request)) return GS232B_OK;
    gs232b_error_t error = gs232b_management_network_authorize(dispatcher, request);
    if (error != GS232B_OK) return error;
    if (dispatcher->network_reset_pending) return GS232B_ERROR_BUSY;
    if (strcmp(request->cmd, "PROV STOP") == 0 &&
        dispatcher->network_config.trial.state != GS232B_CONFIG_TRIAL_NONE) return GS232B_ERROR_CONFLICT;
    if (strcmp(request->cmd, "PROV START") == 0) {
        gs232b_json_value_t value;
        uint32_t timeout = 0;
        if (gs232b_json_get(request->arguments, "timeout_s", &value) == GS232B_OK &&
            (gs232b_json_uint32(value, &timeout) != GS232B_OK ||
             timeout > GS232B_PROVISIONING_MAX_WINDOW_SECONDS)) return GS232B_ERROR_INVALID_ARGUMENT;
        gs232b_control_activity_t activity;
        error = gs232b_control_activity(&dispatcher->control, &activity);
        if (error != GS232B_OK) return error;
        if (!gs232b_control_idle(&activity, dispatcher->control.sequence_state.state) ||
            gs232b_session_key_valid(dispatcher->control.input_reservation.session)) return GS232B_ERROR_BUSY;
    }
    return GS232B_OK;
}

static const char *provisioning_state(gs232b_provisioning_state_t state)
{
    switch (state) {
    case GS232B_PROVISIONING_ACTIVE: return "active";
    case GS232B_PROVISIONING_EXPIRED: return "expired";
    default: return "off";
    }
}

static int provisioning_status(const gs232b_provisioning_t *provisioning, bool include_password,
    char *output, size_t capacity)
{
    gs232b_provisioning_status_t status;
    if (gs232b_provisioning_snapshot(provisioning, &status) != GS232B_OK) return -1;
    const unsigned remaining = (unsigned)((status.remaining_us + UINT64_C(999999)) / UINT64_C(1000000));
    const unsigned a[] = {(status.ap.address >> 24) & 255U, (status.ap.address >> 16) & 255U,
        (status.ap.address >> 8) & 255U, status.ap.address & 255U};
    const unsigned m[] = {(status.ap.netmask >> 24) & 255U, (status.ap.netmask >> 16) & 255U,
        (status.ap.netmask >> 8) & 255U, status.ap.netmask & 255U};
    const unsigned g[] = {(status.ap.gateway >> 24) & 255U, (status.ap.gateway >> 16) & 255U,
        (status.ap.gateway >> 8) & 255U, status.ap.gateway & 255U};
    /* NOTE: 生成的 AP 字段只包含可打印 ASCII。 */
    if (include_password) {
        return snprintf(output, capacity,
            "{\"state\":\"%s\",\"first_boot\":%s,\"auto_started\":%s,"
            "\"remaining_s\":%u,\"trial_active\":%s,\"trial_recovery\":%s,"
            "\"ap\":{\"ssid\":\"%s\",\"password\":\"%s\","
            "\"address\":\"%u.%u.%u.%u\",\"netmask\":\"%u.%u.%u.%u\","
            "\"gateway\":\"%u.%u.%u.%u\"}}",
            provisioning_state(status.state), status.first_boot ? "true" : "false",
            status.auto_started ? "true" : "false", remaining,
            status.trial_active ? "true" : "false", status.recovery_available ? "true" : "false",
            status.ap.ssid, status.ap.password, a[0], a[1], a[2], a[3],
            m[0], m[1], m[2], m[3], g[0], g[1], g[2], g[3]);
    }
    return snprintf(output, capacity,
        "{\"state\":\"%s\",\"first_boot\":%s,\"auto_started\":%s,"
        "\"remaining_s\":%u,\"trial_active\":%s,\"trial_recovery\":%s,"
        "\"ap\":{\"ssid\":\"%s\",\"address\":\"%u.%u.%u.%u\","
        "\"netmask\":\"%u.%u.%u.%u\",\"gateway\":\"%u.%u.%u.%u\"}}",
        provisioning_state(status.state), status.first_boot ? "true" : "false",
        status.auto_started ? "true" : "false", remaining,
        status.trial_active ? "true" : "false", status.recovery_available ? "true" : "false",
        status.ap.ssid, a[0], a[1], a[2], a[3], m[0], m[1], m[2], m[3],
        g[0], g[1], g[2], g[3]);
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
    if ((strcmp(request->cmd, "PROV START") == 0 || strcmp(request->cmd, "PROV STOP") == 0) &&
        capacity < GS232B_EXTENSION_DATA_MAX_BYTES) return GS232B_ERROR_BUFFER_TOO_SMALL;
    if (strcmp(request->cmd, "PROV STATUS") == 0) {
        const int n = provisioning_status(&dispatcher->provisioning,
            request->transport == GS232B_TRANSPORT_UART1, (char *)data, capacity);
        if (n < 0 || (size_t)n >= capacity) return GS232B_ERROR_BUFFER_TOO_SMALL;
        *size = (size_t)n;
        return GS232B_OK;
    }
    if (strcmp(request->cmd, "PROV START") == 0) {
        uint32_t timeout_seconds = 0;
        gs232b_json_value_t value;
        if (gs232b_json_get(request->arguments, "timeout_s", &value) == GS232B_OK) {
            const gs232b_error_t error = gs232b_json_uint32(value, &timeout_seconds);
            if (error != GS232B_OK) return error;
        }
        gs232b_error_t error = GS232B_OK;
        if (dispatcher->provisioning.status.state != GS232B_PROVISIONING_ACTIVE) {
            error = gs232b_provisioning_start(&dispatcher->provisioning, timeout_seconds);
        }
        if (error != GS232B_OK) return error;
        const int n = provisioning_status(&dispatcher->provisioning, false,
                                          (char *)data, capacity);
        if (n < 0 || (size_t)n >= capacity) return GS232B_ERROR_BUFFER_TOO_SMALL;
        *size = (size_t)n;
        return GS232B_OK;
    }
    if (strcmp(request->cmd, "PROV STOP") == 0) {
        const gs232b_error_t error = gs232b_provisioning_stop(&dispatcher->provisioning);
        if (error != GS232B_OK) return error;
        const int n = provisioning_status(&dispatcher->provisioning, false, (char *)data, capacity);
        if (n < 0 || (size_t)n >= capacity) return GS232B_ERROR_BUFFER_TOO_SMALL;
        *size = (size_t)n;
        return GS232B_OK;
    }
    if (strcmp(request->cmd, "PROV SCAN") == 0) {
        uint32_t scan_id = 0, cursor = 0;
        gs232b_json_value_t value;
        const bool has_id = gs232b_json_get(request->arguments, "scan_id", &value) == GS232B_OK;
        const bool has_cursor = gs232b_json_get(request->arguments, "cursor", &value) == GS232B_OK;
        if (has_id != has_cursor) return GS232B_ERROR_INVALID_ARGUMENT;
        if (has_id) {
            if (uint_field(request->arguments, "scan_id", &scan_id) != GS232B_OK || scan_id == 0 ||
                uint_field(request->arguments, "cursor", &cursor) != GS232B_OK) {
                return GS232B_ERROR_INVALID_ARGUMENT;
            }
        }
        return gs232b_provisioning_scan_page(&dispatcher->provisioning,
            scan_id, cursor, data, capacity, size);
    }
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
            (void)gs232b_provisioning_set_trial_active(&dispatcher->provisioning, true);
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
        dispatcher->provisioning.active = next.active;
        (void)gs232b_provisioning_set_trial_active(&dispatcher->provisioning,
            next.trial.state != GS232B_CONFIG_TRIAL_NONE);
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
        dispatcher->provisioning.active = next.active;
        (void)gs232b_provisioning_set_trial_active(&dispatcher->provisioning,
            next.trial.state != GS232B_CONFIG_TRIAL_NONE);
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
         .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "PROV", .action = "START", .schema = &prov_start_schema,
         .check = check, .handle = handle, .ctx = dispatcher}
        ,{.domain = "PROV", .action = "STATUS", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "PROV", .action = "STOP", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "PROV", .action = "SCAN", .schema = &prov_scan_schema,
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
        if (error == GS232B_OK) {
            dispatcher->network_status = status;
            const gs232b_error_t provision_error = gs232b_provisioning_init(
                &dispatcher->provisioning, dispatcher->control.clock, active, revision,
                load_error == GS232B_OK, status, dispatcher->provisioning.ap_port,
                dispatcher->provisioning.mac);
            if (provision_error != GS232B_OK) error = provision_error;
        }
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

gs232b_error_t gs232b_dispatcher_provisioning_configure(gs232b_dispatcher_t *dispatcher,
    gs232b_provisioning_ap_port_t ap_port, const uint8_t mac[6])
{
    if (dispatcher == NULL || !dispatcher->initialized || ap_port.start == NULL ||
        ap_port.stop == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    if (dispatcher->control.dispatching) return GS232B_ERROR_BUSY;
    dispatcher->provisioning.ap_port = ap_port;
    if (mac != NULL) memcpy(dispatcher->provisioning.mac, mac,
                             sizeof(dispatcher->provisioning.mac));
    gs232b_error_t error = gs232b_provisioning_make_ap_credentials(dispatcher->provisioning.mac,
        &dispatcher->provisioning.status.ap);
    if (error == GS232B_OK && dispatcher->provisioning.status.state == GS232B_PROVISIONING_ACTIVE &&
        ap_port.start != NULL) {
        error = ap_port.start(ap_port.ctx, &dispatcher->provisioning.status.ap);
    }
    return error;
}
