#include "gs232b/types.h"
#include "gs232b/coordinates.h"

#include <stddef.h>
#include <string.h>

const char *gs232b_error_name(gs232b_error_t error)
{
    switch (error) {
    case GS232B_OK: return "OK";
    case GS232B_ERROR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case GS232B_ERROR_BUSY: return "BUSY";
    case GS232B_ERROR_NOT_OWNER: return "NOT_OWNER";
    case GS232B_ERROR_NOT_AUTHORIZED: return "NOT_AUTHORIZED";
    case GS232B_ERROR_NOT_READY: return "NOT_READY";
    case GS232B_ERROR_CONFLICT: return "CONFLICT";
    case GS232B_ERROR_TIMEOUT: return "TIMEOUT";
    case GS232B_ERROR_HARDWARE_FAULT: return "HARDWARE_FAULT";
    case GS232B_ERROR_IO: return "IO";
    case GS232B_ERROR_NOT_FOUND: return "NOT_FOUND";
    case GS232B_ERROR_WOULD_BLOCK: return "WOULD_BLOCK";
    case GS232B_ERROR_RESOURCE_EXHAUSTED: return "RESOURCE_EXHAUSTED";
    case GS232B_ERROR_BUFFER_TOO_SMALL: return "BUFFER_TOO_SMALL";
    case GS232B_ERROR_STALE_SESSION: return "STALE_SESSION";
    case GS232B_ERROR_CLOSED: return "CLOSED";
    }
    return "UNKNOWN";
}

bool gs232b_session_key_valid(gs232b_session_key_t key)
{
    return key.session_id != 0 && key.generation != 0;
}

bool gs232b_session_key_equal(gs232b_session_key_t left, gs232b_session_key_t right)
{
    return left.session_id == right.session_id && left.generation == right.generation;
}

bool gs232b_command_context_current(gs232b_command_context_t command,
                                    gs232b_session_key_t live_session,
                                    gs232b_command_epoch_t current_epoch)
{
    return gs232b_session_key_valid(live_session) && current_epoch != 0 &&
           gs232b_session_key_equal(command.session, live_session) &&
           command.epoch == current_epoch;
}

gs232b_error_t gs232b_command_epoch_advance(gs232b_command_epoch_t *epoch)
{
    if (epoch == NULL || *epoch == 0) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (*epoch == UINT64_MAX) {
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    ++*epoch;
    return GS232B_OK;
}

bool gs232b_output_frame_valid(const gs232b_output_frame_t *frame)
{
    return frame != NULL &&
           frame->azimuth >= GS232B_AZ_STOP && frame->azimuth <= GS232B_AZ_CCW &&
           frame->elevation >= GS232B_EL_STOP && frame->elevation <= GS232B_EL_DOWN &&
           frame->speed_permille <= 1000;
}

void gs232b_output_snapshot_init(gs232b_output_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->requested.w5500_reset_asserted = true;
    snapshot->last_successful.w5500_reset_asserted = true;
}

void gs232b_state_snapshot_init(gs232b_state_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->command_epoch = 1;
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        snapshot->axes[axis].axis = (gs232b_axis_id_t)axis;
        snapshot->axes[axis].sample_error = GS232B_ERROR_NOT_READY;
    }
    snapshot->axes[GS232B_AXIS_AZIMUTH].minimum_mdeg = GS232B_AZIMUTH_MIN_MDEG;
    snapshot->axes[GS232B_AXIS_AZIMUTH].maximum_mdeg = GS232B_AZIMUTH_MAX_MDEG;
    snapshot->axes[GS232B_AXIS_ELEVATION].minimum_mdeg = GS232B_ELEVATION_MIN_MDEG;
    snapshot->axes[GS232B_AXIS_ELEVATION].maximum_mdeg = GS232B_ELEVATION_MAX_MDEG;
    gs232b_output_snapshot_init(&snapshot->output);
}
