#include "command_control_internal.h"

#include <string.h>

static bool is(const gs232b_control_request_t *request, const char *text)
{
    return request->position == NULL &&
           gs232b_byte_span_equal_ascii_ci(request->line, text);
}

static gs232b_interaction_t entry(const gs232b_control_request_t *request)
{
    return is(request, "O") ? GS232B_INTERACTION_ZERO_AZ :
           is(request, "O2") ? GS232B_INTERACTION_ZERO_EL :
           is(request, "F") ? GS232B_INTERACTION_DISPLAY_AZ :
           is(request, "F2") ? GS232B_INTERACTION_DISPLAY_BOTH : GS232B_INTERACTION_NONE;
}

static bool displaying(gs232b_interaction_t kind)
{
    return kind == GS232B_INTERACTION_DISPLAY_AZ ||
           kind == GS232B_INTERACTION_DISPLAY_BOTH;
}

bool gs232b_control_calibration_handles(const gs232b_control_t *control,
                                        const gs232b_control_request_t *request)
{
    return request->kind == GS232B_LINE_GS &&
        (entry(request) != GS232B_INTERACTION_NONE || is(request, "Y") ||
         is(request, "YES") || is(request, "NO") ||
         (control->calibration.kind != GS232B_INTERACTION_NONE &&
          gs232b_session_key_equal(control->calibration.session, request->context.session)));
}

static gs232b_error_t samples(gs232b_control_t *control, gs232b_interaction_t kind,
                               gs232b_motion_snapshot_t *state)
{
    gs232b_motion_t *motion = control->executor.calibration.motion;
    gs232b_error_t error = gs232b_motion_snapshot(motion, state);
    if (error != GS232B_OK) {
        return error;
    }
    const gs232b_time_us_t now_us = control->clock.now_us(control->clock.ctx);
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        const bool used = kind == GS232B_INTERACTION_DISPLAY_BOTH ||
            (axis == GS232B_AXIS_AZIMUTH && kind != GS232B_INTERACTION_ZERO_EL) ||
            (axis == GS232B_AXIS_ELEVATION && kind == GS232B_INTERACTION_ZERO_EL);
        if (!used) {
            continue;
        }
        if (!motion->config.axes[axis].enabled) {
            return GS232B_ERROR_NOT_READY;
        }
        error = gs232b_feedback_sample_validate(&state->axes[axis].sample, now_us);
        if (error != GS232B_OK) {
            return error;
        }
        if (displaying(kind) && (!state->axes[axis].position_valid ||
                                state->axes[axis].sample_error != GS232B_OK)) {
            return state->axes[axis].sample_error != GS232B_OK ?
                state->axes[axis].sample_error : GS232B_ERROR_NOT_READY;
        }
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_control_calibration_validate(gs232b_control_t *control,
    const gs232b_control_request_t *request)
{
    const gs232b_calibration_t *calibration = &control->calibration;
    if (calibration->kind != GS232B_INTERACTION_NONE) {
        if (!gs232b_session_key_equal(calibration->session, request->context.session)) {
            return GS232B_ERROR_NOT_OWNER;
        }
        if (request->position != NULL) {
            return GS232B_ERROR_BUSY;
        }
        if (request->line.size == 0 || (!displaying(calibration->kind) &&
            (is(request, "Y") || is(request, "YES") || is(request, "N") ||
             is(request, "NO")))) {
            return GS232B_OK;
        }
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const gs232b_interaction_t kind = entry(request);
    if (kind == GS232B_INTERACTION_NONE) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_control_activity_t activity;
    gs232b_error_t error = gs232b_control_activity(control, &activity);
    if (error != GS232B_OK || activity.faulted) {
        return error != GS232B_OK ? error : GS232B_ERROR_HARDWARE_FAULT;
    }
    if (!gs232b_control_idle(&activity, control->sequence_state.state) ||
        control->sequence.active.point_count != 0 ||
        gs232b_session_key_valid(control->input_reservation.session)) {
        return GS232B_ERROR_BUSY;
    }
    gs232b_motion_snapshot_t state;
    return samples(control, kind, &state);
}

void gs232b_control_calibration_cancel(gs232b_control_t *control)
{
    gs232b_control_session_t *slot = gs232b_control_find(control,
        control->calibration.session);
    if (slot != NULL) {
        gs232b_line_parser_invalidate(&slot->parser);
    }
    memset(&control->calibration, 0, sizeof(control->calibration));
}

static gs232b_error_t frame(gs232b_control_t *control, gs232b_interaction_t kind,
                             gs232b_response_t *response)
{
    gs232b_motion_snapshot_t state;
    gs232b_error_t error = samples(control, kind, &state);
    gs232b_position_context_t position;
    if (error == GS232B_OK) {
        error = control->executor.position_context(control->executor.ctx, &position);
    }
    *response = (gs232b_response_t){
        .kind = kind == GS232B_INTERACTION_DISPLAY_AZ ?
            GS232B_RESPONSE_CALIBRATION_AZIMUTH : GS232B_RESPONSE_CALIBRATION_BOTH
    };
    if (error == GS232B_OK) {
        error = gs232b_azimuth_position_to_display(&position.display,
            state.axes[GS232B_AXIS_AZIMUTH].position_mdeg,
            &response->data.angles.azimuth_degrees);
    }
    if (error == GS232B_OK && kind == GS232B_INTERACTION_DISPLAY_BOTH) {
        error = gs232b_elevation_position_to_display(true,
            state.axes[GS232B_AXIS_ELEVATION].position_mdeg,
            &response->data.angles.elevation_degrees);
    }
    return error;
}

gs232b_error_t gs232b_control_calibration_execute(gs232b_control_t *control,
    const gs232b_control_request_t *request, gs232b_response_t *response)
{
    gs232b_calibration_t *calibration = &control->calibration;
    if (calibration->kind != GS232B_INTERACTION_NONE) {
        const gs232b_axis_id_t axis = calibration->kind == GS232B_INTERACTION_ZERO_EL ?
            GS232B_AXIS_ELEVATION : GS232B_AXIS_AZIMUTH;
        const bool saving = is(request, "Y") || is(request, "YES");
        gs232b_control_calibration_cancel(control);
        *response = (gs232b_response_t){.kind = GS232B_RESPONSE_SUCCESS};
        if (saving) {
            const gs232b_error_t error = gs232b_motion_save_zero(
                control->executor.calibration.motion,
                &control->executor.calibration.store, axis);
            response->kind = error == GS232B_OK ? GS232B_RESPONSE_CALIBRATION_COMPLETE :
                                                GS232B_RESPONSE_ERROR;
            return error;
        }
        return GS232B_OK;
    }
    const gs232b_interaction_t kind = entry(request);
    const gs232b_time_us_t now_us = control->clock.now_us(control->clock.ctx);
    *calibration = (gs232b_calibration_t){
        .kind = kind,
        .session = request->context.session,
        .started_at_us = now_us,
        .checked_at_us = now_us,
        .displayed_at_us = now_us
    };
    if (displaying(kind)) {
        const gs232b_error_t error = frame(control, kind, response);
        if (error != GS232B_OK) {
            gs232b_control_calibration_cancel(control);
        }
        return error;
    }
    *response = (gs232b_response_t){.kind = kind == GS232B_INTERACTION_ZERO_AZ ?
        GS232B_RESPONSE_ZERO_AZIMUTH_PROMPT : GS232B_RESPONSE_ZERO_ELEVATION_PROMPT};
    return GS232B_OK;
}

gs232b_error_t gs232b_control_calibration_tick(gs232b_control_t *control)
{
    gs232b_calibration_t *calibration = &control->calibration;
    if (calibration->kind == GS232B_INTERACTION_NONE) {
        return GS232B_OK;
    }
    if (gs232b_control_find(control, calibration->session) == NULL) {
        gs232b_control_calibration_cancel(control);
        return GS232B_OK;
    }
    const gs232b_time_us_t now_us = control->clock.now_us(control->clock.ctx);
    gs232b_error_t error = now_us < calibration->checked_at_us ||
        now_us - calibration->started_at_us >= GS232B_CALIBRATION_TIMEOUT_US ?
        GS232B_ERROR_TIMEOUT : GS232B_OK;
    calibration->checked_at_us = now_us;
    if (error == GS232B_OK) {
        gs232b_control_activity_t activity;
        error = gs232b_control_activity(control, &activity);
        if (error == GS232B_OK && activity.faulted) {
            error = GS232B_ERROR_HARDWARE_FAULT;
        }
    }
    gs232b_response_t response;
    if (error == GS232B_OK && displaying(calibration->kind)) {
        error = frame(control, calibration->kind, &response);
        if (error == GS232B_OK &&
            now_us - calibration->displayed_at_us < GS232B_CALIBRATION_DISPLAY_PERIOD_US) {
            return GS232B_OK;
        }
    } else if (error == GS232B_OK) {
        return GS232B_OK;
    }
    const gs232b_session_key_t session = calibration->session;
    if (error != GS232B_OK) {
        gs232b_control_calibration_cancel(control);
        response = (gs232b_response_t){.kind = GS232B_RESPONSE_ERROR};
    } else {
        /* NOTE: 只输出当前帧；迟到不追赶旧帧，周期输出不刷新交互期限。 */
        calibration->displayed_at_us = now_us;
    }
    const gs232b_error_t send_error = control->executor.respond(
        control->executor.ctx, session, &response);
    if (send_error != GS232B_OK) {
        gs232b_control_session_close(control, session);
    }
    return error != GS232B_OK ? error : send_error;
}
