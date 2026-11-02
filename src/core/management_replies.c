#include "gs_dispatcher_internal.h"
#include "management_crypto.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t *bytes;
    size_t capacity, size;
    gs232b_error_t error;
} writer_t;

static void append(writer_t *writer, const char *format, ...)
{
    if (writer->error != GS232B_OK) return;
    va_list args;
    va_start(args, format);
    const int count = vsnprintf((char *)writer->bytes + writer->size,
        writer->capacity - writer->size, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= writer->capacity - writer->size) {
        writer->error = GS232B_ERROR_BUFFER_TOO_SMALL;
    } else writer->size += (size_t)count;
}

static const char *boolean(bool value) { return value ? "true" : "false"; }

static const char *axis_mode(gs232b_axis_mode_t mode)
{
    static const char *const names[] = {"disabled", "idle", "manual", "goto", "reversal_wait", "fault"};
    return mode <= GS232B_AXIS_FAULT ? names[mode] : "unknown";
}

static const char *transport(gs232b_transport_t value)
{
    static const char *const names[] = {"uart1", "ethernet_ws", "wifi_ws", "softap_management_ws"};
    return value <= GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS ? names[value] : "unknown";
}

static void reason(writer_t *writer, bool *first, bool active, const char *name)
{
    if (!active) return;
    append(writer, "%s\"%s\"", *first ? "" : ",", name);
    *first = false;
}

static gs232b_error_t owner_status(gs232b_dispatcher_t *dispatcher, writer_t *writer)
{
    gs232b_control_snapshot_t state;
    gs232b_error_t error = gs232b_control_snapshot(&dispatcher->control, &state);
    if (error != GS232B_OK) return error;
    const bool owned = gs232b_session_key_valid(state.owner);
    const bool sequence = state.sequence.state == GS232B_SEQUENCE_LOADED_WAIT_T ||
        state.sequence.state == GS232B_SEQUENCE_RUNNING;
    const char *mode = state.activity.faulted ? "fault" :
        state.activity.interaction != GS232B_INTERACTION_NONE ? "calibration" :
        sequence ? "sequence" : state.activity.axes_active[0] || state.activity.axes_active[1] ?
        "motion" : "idle";
    append(writer, "{\"owner\":");
    if (owned) append(writer, "{\"session_id\":%" PRIu32 ",\"generation\":%" PRIu64
        ",\"transport\":\"%s\"}", state.owner.session_id, state.owner.generation,
        transport(state.owner_transport));
    else append(writer, "null");
    append(writer, ",\"epoch\":%" PRIu64 ",\"mode\":\"%s\",\"reasons\":[", state.epoch, mode);
    bool first = true;
    reason(writer, &first, state.activity.axes_active[0], "azimuth");
    reason(writer, &first, state.activity.axes_active[1], "elevation");
    reason(writer, &first, !state.activity.output_stopped, "output_not_stopped");
    reason(writer, &first, sequence, "sequence");
    reason(writer, &first, state.activity.interaction != GS232B_INTERACTION_NONE, "calibration");
    reason(writer, &first, gs232b_session_key_valid(state.input_reservation), "upload");
    reason(writer, &first, state.activity.faulted, "fault");
    append(writer, "],\"sequence\":{\"state\":%u,\"current_point\":%u,\"point_count\":%u},"
        "\"pending_requests\":%u,\"output_stopped\":%s,\"idle_timer_running\":%s,"
        "\"idle_since_us\":%" PRIu64 "}", (unsigned)state.sequence.state,
        state.sequence.current_point, state.sequence.point_count, (unsigned)state.pending_requests,
        boolean(state.activity.output_stopped), boolean(state.idle_timer_running), state.idle_since_us);
    return writer->error;
}

static void nullable_position(writer_t *writer, bool valid, int32_t value)
{
    if (valid) append(writer, "%" PRId32, value);
    else append(writer, "null");
}

static gs232b_error_t motion_status(gs232b_dispatcher_t *dispatcher, writer_t *writer)
{
    gs232b_motion_snapshot_t state;
    const gs232b_error_t error = gs232b_motion_snapshot(dispatcher->motion, &state);
    if (error != GS232B_OK) return error;
    append(writer, "{\"captured_at_us\":%" PRIu64 ",\"axes\":[", state.captured_at_us);
    for (size_t i = 0; i < GS232B_AXIS_COUNT; ++i) {
        const gs232b_axis_snapshot_t *axis = &state.axes[i];
        const unsigned direction = i == GS232B_AXIS_AZIMUTH ?
            (unsigned)state.output.last_successful.azimuth : (unsigned)state.output.last_successful.elevation;
        append(writer, "%s{\"axis\":\"%s\",\"mode\":\"%s\",\"position_mdeg\":",
            i == 0 ? "" : ",", i == 0 ? "azimuth" : "elevation", axis_mode(axis->mode));
        nullable_position(writer, axis->position_valid, axis->position_mdeg);
        append(writer, ",\"position_valid\":%s,\"target_mdeg\":", boolean(axis->position_valid));
        nullable_position(writer, axis->target_valid, axis->target_mdeg);
        append(writer, ",\"target_valid\":%s,\"zero_valid\":%s,\"sample\":{"
            "\"raw\":%u,\"adc_uv\":%" PRIu32 ",\"acquired_at_us\":%" PRIu64
            ",\"valid\":%s,\"error\":\"%s\"},\"direction\":",
            boolean(axis->target_valid), boolean(axis->zero_valid), axis->sample.raw,
            axis->sample.adc_uv, axis->sample.acquired_at_us, boolean(axis->sample.valid),
            gs232b_error_name(axis->sample_error));
        if (state.output.known) append(writer, "%u", direction);
        else append(writer, "null");
        append(writer, ",\"limits\":{\"minimum_mdeg\":%" PRId32 ",\"maximum_mdeg\":%" PRId32
            ",\"valid\":%s,\"at_minimum\":%s,\"at_maximum\":%s}}",
            axis->minimum_mdeg, axis->maximum_mdeg, boolean(axis->limits_valid),
            boolean(axis->position_valid && axis->limits_valid && axis->position_mdeg <= axis->minimum_mdeg),
            boolean(axis->position_valid && axis->limits_valid && axis->position_mdeg >= axis->maximum_mdeg));
    }
    append(writer, "],\"output_known\":%s,\"speed_permille\":%u,\"fault\":{\"code\":%u,"
        "\"error\":\"%s\",\"axis_mask\":%u,\"occurred_at_us\":%" PRIu64 "}}",
        boolean(state.output.known), state.output.last_successful.speed_permille,
        (unsigned)state.fault.code, gs232b_error_name(state.fault.error),
        state.fault.axis_mask, state.fault.occurred_at_us);
    return writer->error;
}

gs232b_error_t gs232b_management_status(gs232b_dispatcher_t *dispatcher,
    const char *command, uint8_t *data, size_t capacity, size_t *size)
{
    writer_t writer = {data, capacity, 0, GS232B_OK};
    gs232b_error_t error = GS232B_OK;
    if (strncmp(command, "OWNER ", 6) == 0) error = owner_status(dispatcher, &writer);
    else if (strcmp(command, "MOTION STATUS") == 0) error = motion_status(dispatcher, &writer);
    else if (strcmp(command, "SYS INFO") == 0) {
        append(&writer, "{\"version\":");
        const char *version = gs232b_management_version();
        size_t quoted_size = 0;
        if (writer.error == GS232B_OK) {
            writer.error = gs232b_json_string_encode(
                (gs232b_byte_span_t){(const uint8_t *)version, strlen(version)},
                data + writer.size, capacity - writer.size, &quoted_size);
            if (writer.error == GS232B_OK) writer.size += quoted_size;
        }
        append(&writer, ",\"board\":\"ESP32-C3-WROOM-02U-N4\",\"capabilities\":{"
            "\"azimuth\":%s,\"elevation\":%s,\"management_ws\":true,\"application_watchdog\":true,"
            "\"network_admin_enabled\":%s},\"schema\":{\"record\":%u,\"network\":%u,"
            "\"display\":%u,\"zero\":%u,\"admin\":%u},\"entries\":{\"uart1\":%s,"
            "\"ethernet_ws\":%s,\"wifi_ws\":%s,\"softap_management_ws\":%s}}",
            boolean(dispatcher->motion->config.axes[0].enabled), boolean(dispatcher->motion->config.axes[1].enabled),
            boolean(dispatcher->admin_enabled), GS232B_PERSISTENCE_RECORD_FORMAT_VERSION,
            GS232B_PERSISTENCE_SCHEMA_NETWORK, GS232B_PERSISTENCE_SCHEMA_DISPLAY,
            GS232B_FEEDBACK_ZERO_SCHEMA_VERSION, GS232B_PERSISTENCE_SCHEMA_ADMIN,
            boolean(dispatcher->entries_available[0]), boolean(dispatcher->entries_available[1]),
            boolean(dispatcher->entries_available[2]), boolean(dispatcher->entries_available[3] &&
                dispatcher->provisioning.status.state == GS232B_PROVISIONING_ACTIVE));
    } else if (strncmp(command, "SESSION ", 8) == 0) {
        append(&writer, "{\"timeout_ms\":%" PRIu64 ",\"enabled\":%s}",
            dispatcher->watchdog_timeout_us / 1000U, boolean(dispatcher->watchdog_timeout_us != 0));
    } else if (strcmp(command, "FAULT CLEAR") == 0) append(&writer, "{\"cleared\":true}");
    else if (strcmp(command, "NET RESET") == 0) append(&writer,
        "{\"reset\":\"NETWORK_ONLY\",\"configured\":false,\"pending\":%s}",
        boolean(dispatcher->network_reset_pending));
    else return GS232B_ERROR_NOT_FOUND;
    if (error == GS232B_OK) error = writer.error;
    if (error == GS232B_OK) *size = writer.size;
    return error;
}
