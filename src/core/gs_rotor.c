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

#include <stdio.h>
#include <string.h>

static const gs232b_json_field_t fields[] = {
    {.name = "model_id", .type = GS232B_JSON_NUMBER, .required = true}
};
static const gs232b_json_schema_t schema = {fields, 1};

static gs232b_error_t requested_model(const gs232b_extension_request_t *request,
    uint16_t *model_id)
{
    gs232b_json_value_t value;
    uint32_t id = 0;
    if (gs232b_json_get(request->arguments, "model_id", &value) != GS232B_OK ||
        gs232b_json_uint32(value, &id) != GS232B_OK ||
        !gs232b_rotor_model_valid(id)) return GS232B_ERROR_INVALID_ARGUMENT;
    *model_id = (uint16_t)id;
    return GS232B_OK;
}

static gs232b_error_t check(void *ctx, const gs232b_extension_request_t *request)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    if (request->transport == GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS) {
        return GS232B_ERROR_NOT_AUTHORIZED;
    }
    if (strcmp(request->cmd, "ROTOR GET") == 0) return GS232B_OK;
    gs232b_error_t error = gs232b_management_network_authorize(dispatcher, request);
    if (error != GS232B_OK) return error;
    uint16_t model_id;
    error = requested_model(request, &model_id);
    if (error != GS232B_OK) return error;
    const gs232b_control_t *control = &dispatcher->control;
    gs232b_control_activity_t activity;
    error = gs232b_control_activity(control, &activity);
    if (error != GS232B_OK) return error;
    if (!gs232b_control_idle(&activity, control->sequence_state.state) ||
        control->stop_mask != 0 || control->full_stop_pending ||
        gs232b_session_key_valid(control->input_reservation.session)) return GS232B_ERROR_BUSY;
    gs232b_output_manager_snapshot_t output;
    error = gs232b_output_manager_snapshot(dispatcher->motion->manager, &output);
    if (error != GS232B_OK) return error;
    if (output.requested.azimuth != GS232B_AZ_STOP ||
        output.requested.elevation != GS232B_EL_STOP ||
        output.requested.speed_permille != 0) return GS232B_ERROR_BUSY;
    return GS232B_OK;
}

static const char *boolean(bool value) { return value ? "true" : "false"; }

static gs232b_error_t handle(void *ctx, const gs232b_extension_request_t *request,
    uint8_t *data, size_t capacity, size_t *size)
{
    gs232b_dispatcher_t *dispatcher = ctx;
    const bool setting = strcmp(request->cmd, "ROTOR SET") == 0;
    uint16_t model_id = dispatcher->rotor_model_id;
    if (setting) {
        const gs232b_error_t error = requested_model(request, &model_id);
        if (error != GS232B_OK) return error;
    }
    gs232b_motion_config_t configured;
    const gs232b_error_t config_error = gs232b_rotor_config(model_id, &configured);
    if (config_error != GS232B_OK) return config_error;
    const gs232b_motion_config_t *active = &dispatcher->motion->config;
    char prepared[256];
    const int n = snprintf(prepared, sizeof(prepared),
        "{\"model_id\":%u,\"active_model_id\":%u,\"capabilities\":{\"azimuth\":%s,\"elevation\":%s},"
        "\"configured_capabilities\":{\"azimuth\":%s,\"elevation\":%s},\"restart_required\":%s}",
        (unsigned)model_id, (unsigned)dispatcher->active_model_id,
        boolean(active->axes[GS232B_AXIS_AZIMUTH].enabled),
        boolean(active->axes[GS232B_AXIS_ELEVATION].enabled),
        boolean(configured.axes[GS232B_AXIS_AZIMUTH].enabled),
        boolean(configured.axes[GS232B_AXIS_ELEVATION].enabled),
        boolean(model_id != dispatcher->active_model_id));
    /* NOTE: 先保证完整响应可发送，再写入；保存后绝不触碰当前运动配置或重启设备。 */
    if (n < 0 || (size_t)n >= sizeof(prepared) || (size_t)n > capacity) {
        return GS232B_ERROR_BUFFER_TOO_SMALL;
    }
    if (setting && (model_id != dispatcher->rotor_model_id || dispatcher->rotor_revision == 0)) {
        uint64_t revision = 0;
        const gs232b_error_t error = gs232b_rotor_save(&dispatcher->store,
            dispatcher->rotor_revision, model_id, &revision);
        if (error != GS232B_OK) return error;
        dispatcher->rotor_model_id = model_id;
        dispatcher->rotor_revision = revision;
    }
    memcpy(data, prepared, (size_t)n);
    *size = (size_t)n;
    return GS232B_OK;
}

gs232b_error_t gs232b_rotor_register(gs232b_dispatcher_t *dispatcher)
{
    gs232b_error_t error = gs232b_rotor_load(&dispatcher->store,
        &dispatcher->rotor_model_id, &dispatcher->rotor_revision);
    if (error != GS232B_OK) return error;
    const gs232b_extension_handler_t handlers[] = {
        {.domain = "ROTOR", .action = "GET", .check = check, .handle = handle, .ctx = dispatcher},
        {.domain = "ROTOR", .action = "SET", .schema = &schema,
         .check = check, .handle = handle, .ctx = dispatcher}
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        error = gs232b_extension_register(&dispatcher->extensions, &handlers[i]);
        if (error != GS232B_OK) return error;
    }
    return GS232B_OK;
}
