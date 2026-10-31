#include "gs232b/spi_bus.h"

#include <string.h>

static gs232b_error_t event(gs232b_fake_spi_bus_t *fake, spi_event_kind_t kind,
                            uint32_t value, uint16_t address, size_t size)
{
    pthread_mutex_lock(&fake->trace_mutex);
    if (fake->count == SPI_TRACE_CAPACITY) {
        atomic_store(&fake->safe, false);
    } else {
        fake->trace[fake->count++] = (spi_event_t){
            .kind = kind, .value = value, .address = address, .size = size
        };
    }
    atomic_fetch_add(&fake->now_us, fake->latency_us);
    pthread_mutex_unlock(&fake->trace_mutex);
    if (fake->hook != NULL) { fake->hook(fake->hook_ctx, kind); }
    if (fake->fault_kind == kind && fake->fault_error != GS232B_OK) {
        if (fake->fault_skip > 0) {
            --fake->fault_skip;
        } else {
            const gs232b_error_t error = fake->fault_error;
            fake->fault_error = GS232B_OK;
            return error;
        }
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_fake_spi_bus_event(gs232b_fake_spi_bus_t *fake,
                                        spi_event_kind_t kind, uint32_t value)
{
    return event(fake, kind, value, 0, 0);
}

static gs232b_error_t lock(void *ctx)
{
    gs232b_fake_spi_bus_t *fake = ctx;
    if (fake->lock_error != GS232B_OK) {
        const gs232b_error_t error = fake->lock_error;
        fake->lock_error = GS232B_OK;
        return error;
    }
    return pthread_mutex_lock(&fake->entry) == 0 ? GS232B_OK : GS232B_ERROR_IO;
}

static void unlock(void *ctx)
{
    pthread_mutex_unlock(&((gs232b_fake_spi_bus_t *)ctx)->entry);
}

static gs232b_error_t acquire(void *ctx)
{
    gs232b_fake_spi_bus_t *fake = ctx;
    pthread_mutex_lock(&fake->host);
    const gs232b_error_t error = gs232b_fake_spi_bus_event(fake, SPI_ACQUIRE, 0);
    if (error != GS232B_OK) {
        (void)gs232b_fake_spi_bus_event(fake, SPI_ACQUIRE_FAILED, 0);
        pthread_mutex_unlock(&fake->host);
        return error;
    }
    fake->owned = true;
    fake->complete = false;
    return GS232B_OK;
}

static gs232b_error_t transmit(void *ctx, uint8_t byte)
{
    gs232b_fake_spi_bus_t *fake = ctx;
    if (!fake->owned || fake->latch_high) { atomic_store(&fake->safe, false); }
    fake->complete = false;
    const gs232b_error_t error = gs232b_fake_spi_bus_event(fake, SPI_SHIFT_BYTE, byte);
    fake->shifted = error == GS232B_OK ? byte : (uint8_t)(byte ^ 0xffU);
    if (error != GS232B_OK) { return error; }
    const gs232b_error_t completed = gs232b_fake_spi_bus_event(fake, SPI_COMPLETE, 8);
    fake->complete = completed == GS232B_OK;
    return completed;
}

static gs232b_error_t latch(void *ctx, bool high)
{
    gs232b_fake_spi_bus_t *fake = ctx;
    if (!fake->owned || (high && (!fake->complete || fake->latch_high))) {
        atomic_store(&fake->safe, false);
    }
    if (high) { fake->latched = fake->shifted; }
    fake->latch_high = high;
    return gs232b_fake_spi_bus_event(fake, high ? SPI_LATCH_HIGH : SPI_LATCH_LOW,
                                     fake->latched);
}

static void release(void *ctx)
{
    gs232b_fake_spi_bus_t *fake = ctx;
    if (!fake->owned || fake->latch_high) { atomic_store(&fake->safe, false); }
    (void)gs232b_fake_spi_bus_event(fake, SPI_RELEASE, 0);
    fake->owned = false;
    pthread_mutex_unlock(&fake->host);
}

static gs232b_error_t ethernet(void *ctx, uint8_t control, uint16_t address,
                               const void *tx, void *rx, size_t size)
{
    gs232b_fake_spi_bus_t *fake = ctx;
    pthread_mutex_lock(&fake->host);
    if (fake->owned || fake->latch_high) { atomic_store(&fake->safe, false); }
    const uint8_t before = fake->latched;
    /* NOTE: 模拟 W5500 最后移出的字节，故意破坏 595 移位内容。 */
    fake->shifted = tx != NULL ? ((const uint8_t *)tx)[size - 1U] : 0xa7;
    const gs232b_error_t error = event(fake, SPI_ETHERNET, control, address, size);
    if (rx != NULL && error == GS232B_OK) { memset(rx, 0xa7, size); }
    if (before != fake->latched) { atomic_store(&fake->safe, false); }
    pthread_mutex_unlock(&fake->host);
    return error;
}

gs232b_error_t gs232b_fake_spi_bus_foreign_transfer(gs232b_fake_spi_bus_t *fake)
{
    /* NOTE: 模拟 SPI host 自己调度的其他 device，不借助适配器入口锁。 */
    const uint8_t byte = 0xdb;
    return ethernet(fake, 4, 0x1234, &byte, NULL, 1);
}

static gs232b_error_t pwm(void *ctx, uint16_t permille)
{
    gs232b_fake_spi_bus_t *fake = ctx;
    fake->pwm = permille;
    return gs232b_fake_spi_bus_event(fake, SPI_PWM, permille);
}

bool gs232b_fake_spi_bus_init(gs232b_fake_spi_bus_t *fake, gs232b_shared_spi_t *bus)
{
    memset(fake, 0, sizeof(*fake));
    atomic_init(&fake->safe, true);
    atomic_init(&fake->now_us, 0);
    if (pthread_mutex_init(&fake->entry, NULL) != 0 ||
        pthread_mutex_init(&fake->host, NULL) != 0 ||
        pthread_mutex_init(&fake->trace_mutex, NULL) != 0) {
        return false;
    }
    return gs232b_shared_spi_init(bus, (gs232b_shared_spi_io_t){
        .ctx = fake, .lock = lock, .unlock = unlock,
        .acquire_595 = acquire, .transmit_595 = transmit, .set_latch = latch,
        .release_595 = release, .transmit_w5500 = ethernet, .set_pwm = pwm
    }) == GS232B_OK;
}

void gs232b_fake_spi_bus_destroy(gs232b_fake_spi_bus_t *fake)
{
    pthread_mutex_destroy(&fake->entry);
    pthread_mutex_destroy(&fake->host);
    pthread_mutex_destroy(&fake->trace_mutex);
}

void gs232b_fake_spi_bus_clear(gs232b_fake_spi_bus_t *fake)
{
    fake->count = 0;
    fake->fault_error = GS232B_OK;
    fake->lock_error = GS232B_OK;
}

void gs232b_fake_spi_bus_fault(gs232b_fake_spi_bus_t *fake, spi_event_kind_t kind,
                               unsigned skip, gs232b_error_t error)
{
    fake->fault_kind = kind;
    fake->fault_skip = skip;
    fake->fault_error = error;
}

gs232b_time_us_t gs232b_fake_spi_bus_now(void *ctx)
{
    return atomic_load(&((gs232b_fake_spi_bus_t *)ctx)->now_us);
}
