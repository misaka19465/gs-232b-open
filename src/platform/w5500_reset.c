#include "gs232b/w5500_reset.h"

static bool valid(const gs232b_w5500_reset_t *reset)
{
    return reset != NULL && reset->outputs != NULL && reset->bus != NULL &&
           reset->lifecycle.quiesce != NULL && reset->lifecycle.resume != NULL;
}

gs232b_error_t gs232b_w5500_reset_init(gs232b_w5500_reset_t *reset,
                                       gs232b_output_manager_t *outputs,
                                       gs232b_shared_spi_t *bus,
                                       gs232b_w5500_lifecycle_t lifecycle,
                                       gs232b_time_us_t reset_us,
                                       gs232b_time_us_t recovery_us)
{
    gs232b_output_manager_snapshot_t snapshot;
    const gs232b_output_driver_t driver = gs232b_shared_spi_output_driver(bus);
    if (reset == NULL || bus == NULL || lifecycle.quiesce == NULL ||
        lifecycle.resume == NULL || reset_us < GS232B_W5500_RESET_MIN_US ||
        recovery_us < GS232B_W5500_RECOVERY_MIN_US ||
        gs232b_output_manager_snapshot(outputs, &snapshot) != GS232B_OK ||
        outputs->driver.ctx != driver.ctx || outputs->driver.apply != driver.apply) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *reset = (gs232b_w5500_reset_t){
        .outputs = outputs, .bus = bus, .lifecycle = lifecycle,
        .reset_us = reset_us, .recovery_us = recovery_us
    };
    return GS232B_OK;
}

static gs232b_error_t fail(gs232b_w5500_reset_t *reset, gs232b_error_t error)
{
    reset->phase = GS232B_W5500_FAILED;
    reset->last_error = error;
    /* NOTE: 不再改写输出，避免覆盖提交失败的 known=false 和运动故障证据。 */
    (void)gs232b_shared_spi_set_ethernet_enabled(reset->bus, false);
    return error;
}

static gs232b_time_us_t now_us(const gs232b_w5500_reset_t *reset)
{
    return reset->outputs->clock.now_us(reset->outputs->clock.ctx);
}

static bool elapsed(const gs232b_w5500_reset_t *reset, gs232b_time_us_t duration)
{
    const gs232b_time_us_t now = now_us(reset);
    return now >= reset->phase_started_us && now - reset->phase_started_us >= duration;
}

gs232b_error_t gs232b_w5500_reset_tick(gs232b_w5500_reset_t *reset)
{
    if (!valid(reset)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (reset->phase >= GS232B_W5500_QUIESCING &&
        reset->phase <= GS232B_W5500_RESUMING && !reset->outputs->state.output.known) {
        return fail(reset, GS232B_ERROR_NOT_READY);
    }
    gs232b_error_t error;
    switch (reset->phase) {
    case GS232B_W5500_QUIESCING:
        error = reset->lifecycle.quiesce(reset->lifecycle.ctx);
        if (error == GS232B_ERROR_WOULD_BLOCK) {
            return GS232B_OK;
        }
        if (error != GS232B_OK) {
            return fail(reset, error);
        }
        error = gs232b_shared_spi_set_ethernet_enabled(reset->bus, false);
        if (error != GS232B_OK) {
            return fail(reset, error);
        }
        error = gs232b_output_manager_set_w5500_reset(reset->outputs, true);
        if (error != GS232B_OK) {
            return fail(reset, error);
        }
        reset->phase_started_us = now_us(reset);
        reset->phase = GS232B_W5500_RESET_LOW;
        return GS232B_OK;
    case GS232B_W5500_RESET_LOW:
        if (!elapsed(reset, reset->reset_us)) {
            return GS232B_OK;
        }
        error = gs232b_output_manager_set_w5500_reset(reset->outputs, false);
        if (error != GS232B_OK) {
            return fail(reset, error);
        }
        reset->phase_started_us = now_us(reset);
        reset->phase = GS232B_W5500_RECOVERING;
        return GS232B_OK;
    case GS232B_W5500_RECOVERING:
        if (!elapsed(reset, reset->recovery_us)) {
            return GS232B_OK;
        }
        error = gs232b_shared_spi_set_ethernet_enabled(reset->bus, true);
        if (error != GS232B_OK) {
            return fail(reset, error);
        }
        reset->phase = GS232B_W5500_RESUMING;
        /* NOTE: 继续执行本次恢复回调；后续 WOULD_BLOCK 重试不再重复开门。 */
        /* fall through */
    case GS232B_W5500_RESUMING:
        error = reset->lifecycle.resume(reset->lifecycle.ctx);
        if (error == GS232B_ERROR_WOULD_BLOCK) {
            return GS232B_OK;
        }
        if (error != GS232B_OK) {
            return fail(reset, error);
        }
        reset->phase = GS232B_W5500_READY;
        return GS232B_OK;
    case GS232B_W5500_FAILED:
        return GS232B_ERROR_NOT_READY;
    default:
        return GS232B_OK;
    }
}

gs232b_error_t gs232b_w5500_reset_begin(gs232b_w5500_reset_t *reset)
{
    if (!valid(reset)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (reset->phase >= GS232B_W5500_QUIESCING &&
        reset->phase <= GS232B_W5500_RESUMING) {
        return GS232B_ERROR_BUSY;
    }
    reset->last_error = GS232B_OK;
    reset->phase = GS232B_W5500_QUIESCING;
    return gs232b_w5500_reset_tick(reset);
}
