#include "command_control_internal.h"

gs232b_error_t gs232b_control_check_reservation(
    const gs232b_control_t *control, gs232b_session_key_t session)
{
    return gs232b_session_key_valid(control->input_reservation.session) &&
           !gs232b_session_key_equal(control->input_reservation.session, session) ?
           GS232B_ERROR_BUSY : GS232B_OK;
}

gs232b_error_t gs232b_control_upload_begin(void *ctx, uint16_t **angles)
{
    gs232b_control_session_t *slot = ctx;
    gs232b_control_t *control = slot->control;
    *angles = NULL;
    gs232b_error_t error = gs232b_control_check(control, slot->partial_context);
    if (error != GS232B_OK) {
        return error;
    }
    if (control->epoch_exhausted) {
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    if (slot->transport == GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS) {
        return GS232B_ERROR_NOT_AUTHORIZED;
    }
    if (gs232b_session_key_valid(control->owner) &&
        !gs232b_session_key_equal(control->owner, slot->key)) {
        return GS232B_ERROR_NOT_OWNER;
    }
    if (control->stop_mask != 0 ||
        gs232b_session_key_valid(control->input_reservation.session)) {
        return GS232B_ERROR_BUSY;
    }
    control->input_reservation = slot->partial_context;
    *angles = gs232b_sequence_store_staging(&control->sequence);
    return GS232B_OK;
}

void gs232b_control_upload_abort(void *ctx)
{
    gs232b_control_session_t *slot = ctx;
    gs232b_control_t *control = slot->control;
    if (!control->upload_queued &&
        gs232b_session_key_equal(control->input_reservation.session, slot->key) &&
        control->input_reservation.epoch == slot->partial_context.epoch) {
        control->input_reservation = (gs232b_command_context_t){0};
    }
}

gs232b_error_t gs232b_control_validate_position(
    gs232b_control_t *control, const gs232b_position_command_t *command,
    gs232b_position_context_t *context, gs232b_position_plan_t *plan)
{
    if (command->format == GS232B_POSITION_BARE) {
        return GS232B_OK;
    }
    gs232b_control_activity_t activity = {0};
    gs232b_error_t error = control->executor.snapshot(control->executor.ctx, &activity);
    if (error != GS232B_OK || activity.faulted) {
        return error != GS232B_OK ? error : GS232B_ERROR_HARDWARE_FAULT;
    }
    if (activity.interaction != GS232B_INTERACTION_NONE) {
        return GS232B_ERROR_BUSY;
    }
    error = control->executor.position_context(control->executor.ctx, context);
    return error != GS232B_OK ? error :
           gs232b_sequence_store_validate(&control->sequence, command, context, plan);
}

void gs232b_control_cancel(gs232b_control_t *control)
{
    gs232b_control_sequence_clear(control);
    control->executor.cancel(control->executor.ctx);
}
