#include "gs232b/output_manager.h"

#include <string.h>

#define EL_UP_BIT UINT8_C(0x01)
#define EL_DOWN_BIT UINT8_C(0x02)
#define AZ_CW_BIT UINT8_C(0x04)
#define AZ_CCW_BIT UINT8_C(0x08)
#define W5500_RELEASE_BIT UINT8_C(0x10)

_Static_assert(GS232B_AZ_STOP == 0 && GS232B_AZ_CW == 1 && GS232B_AZ_CCW == 2,
               "AZ direction history requires STOP/forward/reverse values 0/1/2");
_Static_assert(GS232B_EL_STOP == 0 && GS232B_EL_UP == 1 && GS232B_EL_DOWN == 2,
               "EL direction history requires STOP/forward/reverse values 0/1/2");

static bool manager_valid(const gs232b_output_manager_t *manager)
{
    return manager != NULL && manager->clock.now_us != NULL &&
           manager->driver.apply != NULL && manager->reversal_wait_us != 0;
}

static uint8_t frame_direction(const gs232b_output_frame_t *frame,
                               gs232b_axis_id_t axis)
{
    return axis == GS232B_AXIS_AZIMUTH ? (uint8_t)frame->azimuth :
                                       (uint8_t)frame->elevation;
}

static uint8_t encode_shadow(const gs232b_output_frame_t *frame)
{
    uint8_t shadow = 0;
    if (frame->elevation == GS232B_EL_UP) {
        shadow |= EL_UP_BIT;
    } else if (frame->elevation == GS232B_EL_DOWN) {
        shadow |= EL_DOWN_BIT;
    }
    if (frame->azimuth == GS232B_AZ_CW) {
        shadow |= AZ_CW_BIT;
    } else if (frame->azimuth == GS232B_AZ_CCW) {
        shadow |= AZ_CCW_BIT;
    }
    if (!frame->w5500_reset_asserted) {
        shadow |= W5500_RELEASE_BIT;
    }
    return shadow;
}

static bool frames_equal(const gs232b_output_frame_t *left,
                         const gs232b_output_frame_t *right)
{
    return left->azimuth == right->azimuth && left->elevation == right->elevation &&
           left->speed_permille == right->speed_permille &&
           left->w5500_reset_asserted == right->w5500_reset_asserted;
}

static void cancel_motion(gs232b_output_manager_t *manager)
{
    manager->state.requested.azimuth = GS232B_AZ_STOP;
    manager->state.requested.elevation = GS232B_EL_STOP;
    manager->state.requested.speed_permille = 0;
    memset(manager->state.reversal_waiting, 0, sizeof(manager->state.reversal_waiting));
}

static gs232b_error_t commit(gs232b_output_manager_t *manager,
                              const gs232b_output_frame_t *frame, bool force)
{
    gs232b_output_snapshot_t *output = &manager->state.output;
    if (!force && output->known && frames_equal(frame, &output->last_successful)) {
        return GS232B_OK;
    }

    manager->state.shadow_byte = encode_shadow(frame);
    output->requested = *frame;
    output->attempted_at_us = manager->clock.now_us(manager->clock.ctx);
    const gs232b_error_t error = manager->driver.apply(
        manager->driver.ctx, manager->state.shadow_byte, frame->speed_permille);
    output->last_error = error;
    output->known = error == GS232B_OK;
    output->speed_voltage_confirmed = false;
    if (error != GS232B_OK) {
        /* NOTE: SPI/PWM 可能部分完成；不能用旧的方向历史推断物理状态，
         * 也不能在后续 reset、PWM 更新或 tick 时重新执行失败的运动。 */
        memset(manager->axes, 0, sizeof(manager->axes));
        cancel_motion(manager);
        return error;
    }

    output->last_successful = *frame;
    const gs232b_time_us_t completed_at_us = manager->clock.now_us(manager->clock.ctx);
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        gs232b_output_manager_axis_t *axis = &manager->axes[i];
        const uint8_t direction = frame_direction(frame, (gs232b_axis_id_t)i);
        if (direction == 0) {
            if (!axis->stop_confirmed) {
                axis->stopped_at_us = completed_at_us;
                axis->stop_confirmed = true;
            }
        } else {
            axis->last_direction = direction;
            axis->history_known = true;
            axis->stop_confirmed = false;
        }
    }
    return GS232B_OK;
}

static uint8_t ready_direction(gs232b_output_manager_t *manager,
                               gs232b_axis_id_t axis_id, gs232b_time_us_t now_us)
{
    const uint8_t requested = frame_direction(&manager->state.requested, axis_id);
    const gs232b_output_manager_axis_t *axis = &manager->axes[axis_id];
    bool ready = requested == 0 ||
                 (axis->history_known && requested == axis->last_direction);
    if (!ready && axis->stop_confirmed && now_us >= axis->stopped_at_us &&
        now_us - axis->stopped_at_us >= manager->reversal_wait_us) {
        ready = true;
    }
    manager->state.reversal_waiting[axis_id] = !ready;
    return ready ? requested : 0;
}

static gs232b_error_t advance(gs232b_output_manager_t *manager)
{
    const gs232b_time_us_t now_us = manager->clock.now_us(manager->clock.ctx);
    gs232b_output_frame_t frame = manager->state.requested;
    frame.azimuth = (gs232b_az_direction_t)ready_direction(
        manager, GS232B_AXIS_AZIMUTH, now_us);
    frame.elevation = (gs232b_el_direction_t)ready_direction(
        manager, GS232B_AXIS_ELEVATION, now_us);
    if (frame.azimuth == GS232B_AZ_STOP) {
        frame.speed_permille = 0;
    }
    return commit(manager, &frame, false);
}

gs232b_error_t gs232b_output_manager_init(gs232b_output_manager_t *manager,
                                         gs232b_clock_t clock,
                                         gs232b_output_driver_t driver,
                                         gs232b_time_us_t reversal_wait_us)
{
    if (manager == NULL || clock.now_us == NULL || driver.apply == NULL ||
        reversal_wait_us == 0) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(manager, 0, sizeof(*manager));
    manager->clock = clock;
    manager->driver = driver;
    manager->reversal_wait_us = reversal_wait_us;
    manager->state.requested.w5500_reset_asserted = true;
    gs232b_output_snapshot_init(&manager->state.output);
    return commit(manager, &manager->state.requested, true);
}

gs232b_error_t gs232b_output_manager_apply(gs232b_output_manager_t *manager,
                                          const gs232b_output_frame_t *frame)
{
    if (!manager_valid(manager) || !gs232b_output_frame_valid(frame)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!manager->state.output.known &&
        (frame->azimuth != GS232B_AZ_STOP || frame->elevation != GS232B_EL_STOP)) {
        return GS232B_ERROR_NOT_READY;
    }
    manager->state.requested = *frame;
    return advance(manager);
}

static gs232b_error_t port_apply(void *ctx, const gs232b_output_frame_t *frame)
{
    return gs232b_output_manager_apply(ctx, frame);
}

gs232b_output_t gs232b_output_manager_port(gs232b_output_manager_t *manager)
{
    return (gs232b_output_t){.ctx = manager, .apply = port_apply};
}

gs232b_error_t gs232b_output_manager_set_directions(gs232b_output_manager_t *manager,
                                                   gs232b_az_direction_t azimuth,
                                                   gs232b_el_direction_t elevation)
{
    if (!manager_valid(manager)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_output_frame_t frame = manager->state.requested;
    frame.azimuth = azimuth;
    frame.elevation = elevation;
    return gs232b_output_manager_apply(manager, &frame);
}

gs232b_error_t gs232b_output_manager_set_pwm(gs232b_output_manager_t *manager,
                                            uint16_t speed_permille)
{
    if (!manager_valid(manager) || speed_permille > 1000U) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    manager->state.requested.speed_permille = speed_permille;
    gs232b_output_frame_t frame = manager->state.output.known ?
                                 manager->state.output.last_successful :
                                 manager->state.requested;
    frame.speed_permille = frame.azimuth == GS232B_AZ_STOP ? 0 : speed_permille;
    return commit(manager, &frame, false);
}

gs232b_error_t gs232b_output_manager_set_w5500_reset(gs232b_output_manager_t *manager,
                                                    bool asserted)
{
    if (!manager_valid(manager)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    manager->state.requested.w5500_reset_asserted = asserted;
    gs232b_output_frame_t frame = manager->state.output.known ?
                                 manager->state.output.last_successful :
                                 manager->state.requested;
    frame.w5500_reset_asserted = asserted;
    if (frame.azimuth == GS232B_AZ_STOP) {
        frame.speed_permille = 0;
    }
    return commit(manager, &frame, false);
}

gs232b_error_t gs232b_output_manager_tick(gs232b_output_manager_t *manager)
{
    if (!manager_valid(manager)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!manager->state.output.known) {
        return GS232B_ERROR_NOT_READY;
    }
    return advance(manager);
}

gs232b_error_t gs232b_output_manager_stop_all(gs232b_output_manager_t *manager)
{
    if (!manager_valid(manager)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    cancel_motion(manager);
    return commit(manager, &manager->state.requested, true);
}

gs232b_error_t gs232b_output_manager_snapshot(
    const gs232b_output_manager_t *manager,
    gs232b_output_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!manager_valid(manager)) {
        memset(snapshot, 0, sizeof(*snapshot));
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *snapshot = manager->state;
    return GS232B_OK;
}
