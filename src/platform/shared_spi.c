#include "gs232b/shared_spi.h"

static bool valid(const gs232b_shared_spi_t *bus)
{
    return bus != NULL && bus->io.lock != NULL && bus->io.unlock != NULL &&
           bus->io.acquire_595 != NULL && bus->io.transmit_595 != NULL &&
           bus->io.set_latch != NULL && bus->io.release_595 != NULL &&
           bus->io.transmit_w5500 != NULL && bus->io.set_pwm != NULL;
}

gs232b_error_t gs232b_shared_spi_init(gs232b_shared_spi_t *bus,
                                      gs232b_shared_spi_io_t io)
{
    const gs232b_shared_spi_t candidate = {.io = io};
    if (bus == NULL || !valid(&candidate)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *bus = candidate;
    atomic_init(&bus->ethernet_enabled, false);
    return GS232B_OK;
}

static gs232b_error_t apply(void *ctx, uint8_t byte, uint16_t pwm)
{
    gs232b_shared_spi_t *bus = ctx;
    if (!valid(bus) || (byte & 0xe0U) != 0 || (byte & 3U) == 3U ||
        (byte & 0x0cU) == 0x0cU || pwm > 1000U ||
        ((byte & 0x0cU) == 0 && pwm != 0)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    /* NOTE: 停机先降 PWM；即使失败也尝试清方向，避免 PWM 故障阻止 595 停机。 */
    gs232b_error_t pwm_error = pwm == 0 ? bus->io.set_pwm(bus->io.ctx, 0) : GS232B_OK;
    gs232b_error_t error = bus->io.lock(bus->io.ctx);
    if (error != GS232B_OK) {
        return pwm_error != GS232B_OK ? pwm_error : error;
    }
    error = bus->io.acquire_595(bus->io.ctx);
    if (error == GS232B_OK) {
        error = bus->io.set_latch(bus->io.ctx, false);
        if (error == GS232B_OK) {
            /* NOTE: 不复用以太网污染后的移位内容，每次都移入完整 8bit。 */
            error = bus->io.transmit_595(bus->io.ctx, byte);
        }
        if (error == GS232B_OK) {
            error = bus->io.set_latch(bus->io.ctx, true);
        }
        /* NOTE: 失败后的收尾仍在 host 所有权内；先降 STCP，最后才放行网络。 */
        const gs232b_error_t low_error = bus->io.set_latch(bus->io.ctx, false);
        if (error == GS232B_OK) {
            error = low_error;
        }
        bus->io.release_595(bus->io.ctx);
    }
    bus->io.unlock(bus->io.ctx);
    if (pwm_error != GS232B_OK) {
        return pwm_error;
    }
    if (error != GS232B_OK) {
        return error;
    }
    /* NOTE: 非零 PWM 仅在方向成功锁存后更新；失败的部分状态由管理器标未知。 */
    return pwm != 0 ? bus->io.set_pwm(bus->io.ctx, pwm) : GS232B_OK;
}

gs232b_output_driver_t gs232b_shared_spi_output_driver(gs232b_shared_spi_t *bus)
{
    return (gs232b_output_driver_t){.ctx = bus, .apply = apply};
}

gs232b_error_t gs232b_shared_spi_set_ethernet_enabled(gs232b_shared_spi_t *bus,
                                                     bool enabled)
{
    if (!valid(bus)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!enabled) {
        atomic_store(&bus->ethernet_enabled, false);
    }
    const gs232b_error_t error = bus->io.lock(bus->io.ctx);
    if (error != GS232B_OK) {
        return error;
    }
    atomic_store(&bus->ethernet_enabled, enabled);
    bus->io.unlock(bus->io.ctx);
    return GS232B_OK;
}

gs232b_error_t gs232b_shared_spi_w5500_transfer(gs232b_shared_spi_t *bus,
                                               uint8_t control, uint16_t address,
                                               const void *tx, void *rx, size_t size)
{
    if (!valid(bus) || size == 0 || size > GS232B_SHARED_SPI_MAX_TRANSFER ||
        ((tx == NULL) == (rx == NULL)) || (control & 3U) != 0 ||
        ((control & 4U) != 0) != (tx != NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!atomic_load(&bus->ethernet_enabled)) {
        return GS232B_ERROR_NOT_READY;
    }
    gs232b_error_t error = bus->io.lock(bus->io.ctx);
    if (error != GS232B_OK) {
        return error;
    }
    error = atomic_load(&bus->ethernet_enabled) ? bus->io.transmit_w5500(
        bus->io.ctx, control, address, tx, rx, size) : GS232B_ERROR_NOT_READY;
    bus->io.unlock(bus->io.ctx);
    return error;
}
