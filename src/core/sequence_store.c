#include "gs232b/sequence_store.h"

#include <string.h>

void gs232b_sequence_store_init(gs232b_sequence_store_t *store)
{
    memset(store, 0, sizeof(*store));
}

uint16_t *gs232b_sequence_store_staging(gs232b_sequence_store_t *store)
{
    return store->banks[1U - store->active_bank];
}

void gs232b_sequence_store_clear(gs232b_sequence_store_t *store)
{
    store->active = (gs232b_sequence_data_t){0};
    store->first = (gs232b_position_plan_t){0};
}

static bool ready(const gs232b_axis_snapshot_t *axis)
{
    return axis->zero_valid && axis->position_valid && axis->limits_valid &&
           axis->mode != GS232B_AXIS_DISABLED && axis->mode != GS232B_AXIS_FAULT &&
           axis->minimum_mdeg <= axis->position_mdeg &&
           axis->position_mdeg <= axis->maximum_mdeg;
}

static bool in_limits(const gs232b_axis_snapshot_t *axis, int32_t target)
{
    return target >= axis->minimum_mdeg && target <= axis->maximum_mdeg;
}

static gs232b_error_t azimuth_target(const gs232b_position_context_t *context,
                                     uint16_t degrees, int32_t reference,
                                     int32_t *target)
{
    const gs232b_error_t error = gs232b_azimuth_target_to_position(
        &context->display, degrees, reference, target);
    if (error != GS232B_OK) {
        return error;
    }
    const gs232b_axis_snapshot_t *axis = &context->axes[GS232B_AXIS_AZIMUTH];
    if (!in_limits(axis, *target) && context->display.mode == GS232B_AZIMUTH_DISPLAY_P36) {
        /* NOTE: 固定行程内最近的解可能被软件限位排除，仍须检查另一等价解。 */
        const int32_t other = *target < INT32_C(360000) ?
            *target + INT32_C(360000) : *target - INT32_C(360000);
        if (other >= GS232B_AZIMUTH_MIN_MDEG && other <= GS232B_AZIMUTH_MAX_MDEG &&
            in_limits(axis, other)) {
            *target = other;
        }
    }
    return in_limits(axis, *target) ? GS232B_OK : GS232B_ERROR_INVALID_ARGUMENT;
}

gs232b_error_t gs232b_sequence_store_validate(
    const gs232b_sequence_store_t *store, const gs232b_position_command_t *command,
    const gs232b_position_context_t *context, gs232b_position_plan_t *plan)
{
    if (store == NULL || command == NULL || context == NULL || plan == NULL ||
        (command->command != 'M' && command->command != 'W') ||
        command->angles == NULL || command->point_count == 0 ||
        (command->format != GS232B_POSITION_SINGLE &&
         command->format != GS232B_POSITION_SEQUENCE)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const bool paired = command->command == 'W';
    if ((command->format == GS232B_POSITION_SINGLE && command->point_count != 1) ||
        (command->format == GS232B_POSITION_SEQUENCE &&
         (command->interval_s == 0 || command->interval_s > 999 ||
          command->point_count < 2 || command->point_count >
              (paired ? GS232B_W_SEQUENCE_MAX_PAIRS : GS232B_M_SEQUENCE_MAX_POINTS)))) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!ready(&context->axes[GS232B_AXIS_AZIMUTH]) ||
        (paired && context->elevation_enabled &&
         !ready(&context->axes[GS232B_AXIS_ELEVATION]))) {
        return GS232B_ERROR_NOT_READY;
    }
    *plan = (gs232b_position_plan_t){
        .axis_mask = paired && context->elevation_enabled ? UINT8_C(3) : UINT8_C(1)
    };
    const uint8_t old_mask = store->active.point_count == 0 ? 0 : store->first.axis_mask;
    plan->stop_mask = (uint8_t)(old_mask & ~plan->axis_mask);
    int32_t reference = context->axes[GS232B_AXIS_AZIMUTH].position_mdeg;
    for (size_t point = 0; point < command->point_count; ++point) {
        const size_t offset = point * (paired ? 2U : 1U);
        int32_t azimuth;
        gs232b_error_t error = azimuth_target(context, command->angles[offset],
                                               reference, &azimuth);
        if (error != GS232B_OK) {
            return error;
        }
        int32_t elevation = 0;
        if (paired) {
            const uint16_t degrees = command->angles[offset + 1];
            error = gs232b_elevation_target_validate(context->elevation_enabled, degrees);
            if (error != GS232B_OK) {
                return error;
            }
            elevation = (int32_t)degrees * INT32_C(1000);
            if (context->elevation_enabled &&
                !in_limits(&context->axes[GS232B_AXIS_ELEVATION], elevation)) {
                return GS232B_ERROR_INVALID_ARGUMENT;
            }
        }
        if (point == 0) {
            plan->targets_mdeg[GS232B_AXIS_AZIMUTH] = azimuth;
            plan->targets_mdeg[GS232B_AXIS_ELEVATION] = elevation;
        }
        reference = azimuth;
    }
    return GS232B_OK;
}

void gs232b_sequence_store_commit(gs232b_sequence_store_t *store,
                                   const gs232b_position_command_t *command,
                                   const gs232b_position_context_t *context,
                                   const gs232b_position_plan_t *plan)
{
    if (command->format == GS232B_POSITION_SINGLE) {
        gs232b_sequence_store_clear(store);
        return;
    }
    uint16_t *angles = gs232b_sequence_store_staging(store);
    int32_t reference = context->axes[GS232B_AXIS_AZIMUTH].position_mdeg;
    for (size_t point = 0; point < command->point_count; ++point) {
        const size_t offset = point * (command->command == 'W' ? 2U : 1U);
        int32_t target = 0;
        /* NOTE: 同一串行边界已完整验证原值及冻结的 context，此处转换不会失败。 */
        (void)azimuth_target(context, angles[offset], reference, &target);
        angles[offset] = (uint16_t)(target / INT32_C(1000));
        reference = target;
    }
    store->active_bank = 1U - store->active_bank;
    store->active = (gs232b_sequence_data_t){
        .command = command->command,
        .interval_s = command->interval_s,
        .point_count = command->point_count,
        .angles = angles
    };
    store->first = *plan;
}
