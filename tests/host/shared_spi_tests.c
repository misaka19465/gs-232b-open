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

#include "gs232b/spi_bus.h"

#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static bool assert_atomic_trace(const gs232b_fake_spi_bus_t *fake)
{
    bool acquired = false;
    bool shifted = false;
    bool complete = false;
    bool latched = false;
    for (size_t i = 0; i < fake->count; ++i) {
        const spi_event_t event = fake->trace[i];
        switch (event.kind) {
        case SPI_ACQUIRE:
            CHECK(!acquired);
            acquired = true;
            shifted = complete = latched = false;
            break;
        case SPI_ACQUIRE_FAILED:
            CHECK(acquired && !shifted && !latched);
            acquired = false;
            break;
        case SPI_SHIFT_BYTE:
            CHECK(acquired && !shifted && !latched);
            shifted = true;
            break;
        case SPI_COMPLETE:
            CHECK(acquired && shifted && !latched && event.value == 8);
            complete = true;
            break;
        case SPI_LATCH_HIGH:
            CHECK(acquired && shifted && complete && !latched);
            latched = true;
            break;
        case SPI_LATCH_LOW:
            CHECK(acquired);
            break;
        case SPI_RELEASE:
            CHECK(acquired && i > 0 && fake->trace[i - 1U].kind == SPI_LATCH_LOW);
            acquired = false;
            break;
        case SPI_ETHERNET:
            CHECK(!acquired);
            break;
        default:
            break;
        }
    }
    CHECK(!acquired && atomic_load(&fake->safe));
    return true;
}

static size_t count_kind(const gs232b_fake_spi_bus_t *fake, spi_event_kind_t kind)
{
    size_t count = 0;
    for (size_t i = 0; i < fake->count; ++i) { count += fake->trace[i].kind == kind; }
    return count;
}

static bool test_full_byte_after_ethernet(void)
{
    gs232b_fake_spi_bus_t fake;
    gs232b_shared_spi_t bus;
    CHECK(gs232b_fake_spi_bus_init(&fake, &bus));
    const gs232b_output_driver_t driver = gs232b_shared_spi_output_driver(&bus);
    uint8_t data = 0xff;
    CHECK(gs232b_shared_spi_w5500_transfer(&bus, 4, 0x1234, &data, NULL, 1) ==
          GS232B_ERROR_NOT_READY);
    CHECK(fake.count == 0);
    CHECK(gs232b_shared_spi_set_ethernet_enabled(&bus, true) == GS232B_OK);
    for (unsigned byte = 0; byte < 32; ++byte) {
        if ((byte & 3U) == 3U || (byte & 0x0cU) == 0x0cU) { continue; }
        CHECK(gs232b_shared_spi_w5500_transfer(&bus, 4, 0x1234, &data, NULL, 1) ==
              GS232B_OK);
        CHECK(fake.shifted == 0xff);
        gs232b_fake_spi_bus_clear(&fake);
        CHECK(driver.apply(driver.ctx, (uint8_t)byte, 0) == GS232B_OK);
        static const spi_event_kind_t expected[] = {
            SPI_PWM, SPI_ACQUIRE, SPI_LATCH_LOW, SPI_SHIFT_BYTE, SPI_COMPLETE,
            SPI_LATCH_HIGH, SPI_LATCH_LOW, SPI_RELEASE
        };
        CHECK(fake.count == sizeof(expected) / sizeof(expected[0]));
        for (size_t i = 0; i < fake.count; ++i) { CHECK(fake.trace[i].kind == expected[i]); }
        CHECK(fake.trace[3].value == byte && fake.latched == byte && !fake.latch_high);
        uint8_t rx[3] = {0};
        CHECK(gs232b_shared_spi_w5500_transfer(&bus, 0x08, 0x4567, NULL, rx, 3) ==
              GS232B_OK);
        CHECK(rx[0] == 0xa7 && rx[2] == 0xa7 && fake.latched == byte);
        CHECK(fake.trace[8].value == 0x08 && fake.trace[8].address == 0x4567 &&
              fake.trace[8].size == 3);
        CHECK(assert_atomic_trace(&fake));
    }
    gs232b_fake_spi_bus_destroy(&fake);
    return true;
}

static bool test_failures_release_without_extra_latch(void)
{
    static const struct { spi_event_kind_t kind; unsigned skip; bool latched; } faults[] = {
        {SPI_ACQUIRE, 0, false}, {SPI_LATCH_LOW, 0, false},
        {SPI_SHIFT_BYTE, 0, false}, {SPI_COMPLETE, 0, false},
        {SPI_LATCH_HIGH, 0, true}, {SPI_LATCH_LOW, 1, true}, {SPI_PWM, 0, true}
    };
    for (size_t f = 0; f < sizeof(faults) / sizeof(faults[0]); ++f) {
        gs232b_fake_spi_bus_t fake;
        gs232b_shared_spi_t bus;
        CHECK(gs232b_fake_spi_bus_init(&fake, &bus));
        CHECK(gs232b_shared_spi_set_ethernet_enabled(&bus, true) == GS232B_OK);
        const gs232b_output_driver_t driver = gs232b_shared_spi_output_driver(&bus);
        gs232b_fake_spi_bus_fault(&fake, faults[f].kind, faults[f].skip, GS232B_ERROR_IO);
        CHECK(driver.apply(driver.ctx, 0x15, 0) == GS232B_ERROR_IO);
        CHECK(count_kind(&fake, SPI_LATCH_HIGH) == (faults[f].latched ? 1U : 0U));
        CHECK(count_kind(&fake, SPI_RELEASE) == (faults[f].kind == SPI_ACQUIRE ? 0U : 1U));
        CHECK(!fake.latch_high && !fake.owned);
        const uint8_t data = 0xfe;
        CHECK(gs232b_shared_spi_w5500_transfer(&bus, 4, 0, &data, NULL, 1) == GS232B_OK);
        CHECK(driver.apply(driver.ctx, 0x10, 0) == GS232B_OK);
        CHECK(fake.latched == 0x10 && assert_atomic_trace(&fake));
        gs232b_fake_spi_bus_destroy(&fake);
    }
    return true;
}

typedef struct {
    gs232b_fake_spi_bus_t *fake;
    gs232b_shared_spi_t *bus;
    pthread_mutex_t sync;
    pthread_cond_t changed;
    spi_event_kind_t stage;
    unsigned skip;
    bool start;
    bool attempted;
    bool blocked;
    bool foreign;
    gs232b_error_t result;
} race_t;

static void race_hook(void *ctx, spi_event_kind_t event)
{
    race_t *race = ctx;
    if (event != race->stage || race->start) { return; }
    if (race->skip > 0) { --race->skip; return; }
    pthread_mutex_lock(&race->sync);
    race->start = true;
    pthread_cond_broadcast(&race->changed);
    while (!race->attempted) { pthread_cond_wait(&race->changed, &race->sync); }
    pthread_mutex_unlock(&race->sync);
}

static void *contend(void *ctx)
{
    race_t *race = ctx;
    pthread_mutex_lock(&race->sync);
    while (!race->start) { pthread_cond_wait(&race->changed, &race->sync); }
    pthread_mutex_t *mutex = race->foreign ? &race->fake->host : &race->fake->entry;
    const int result = pthread_mutex_trylock(mutex);
    race->blocked = result == EBUSY;
    if (result == 0) { pthread_mutex_unlock(mutex); }
    race->attempted = true;
    pthread_cond_broadcast(&race->changed);
    pthread_mutex_unlock(&race->sync);
    const uint8_t data = 0xdd;
    race->result = race->foreign ? gs232b_fake_spi_bus_foreign_transfer(race->fake) :
        gs232b_shared_spi_w5500_transfer(race->bus, 4, 0x1122, &data, NULL, 1);
    return NULL;
}

static bool run_race(spi_event_kind_t stage, unsigned skip, bool foreign, bool fault)
{
    gs232b_fake_spi_bus_t fake;
    gs232b_shared_spi_t bus;
    CHECK(gs232b_fake_spi_bus_init(&fake, &bus));
    CHECK(gs232b_shared_spi_set_ethernet_enabled(&bus, true) == GS232B_OK);
    race_t race = {.fake = &fake, .bus = &bus, .stage = stage, .skip = skip,
                   .foreign = foreign};
    CHECK(pthread_mutex_init(&race.sync, NULL) == 0);
    CHECK(pthread_cond_init(&race.changed, NULL) == 0);
    fake.hook = race_hook;
    fake.hook_ctx = &race;
    if (fault) { gs232b_fake_spi_bus_fault(&fake, stage, skip, GS232B_ERROR_IO); }
    pthread_t worker;
    CHECK(pthread_create(&worker, NULL, contend, &race) == 0);
    const gs232b_output_driver_t driver = gs232b_shared_spi_output_driver(&bus);
    const gs232b_error_t error = driver.apply(driver.ctx, 0x15, 1000);
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK(error == (fault ? GS232B_ERROR_IO : GS232B_OK));
    CHECK(race.start && race.attempted && race.blocked && race.result == GS232B_OK);
    CHECK(count_kind(&fake, SPI_ETHERNET) == 1 && assert_atomic_trace(&fake));
    CHECK(count_kind(&fake, SPI_LATCH_HIGH) <= 1);
    pthread_cond_destroy(&race.changed);
    pthread_mutex_destroy(&race.sync);
    gs232b_fake_spi_bus_destroy(&fake);
    return true;
}

static bool test_concurrent_transactions_at_every_boundary(void)
{
    static const struct { spi_event_kind_t kind; unsigned skip; } stages[] = {
        {SPI_ACQUIRE, 0}, {SPI_LATCH_LOW, 0}, {SPI_SHIFT_BYTE, 0},
        {SPI_COMPLETE, 0}, {SPI_LATCH_HIGH, 0}, {SPI_LATCH_LOW, 1}, {SPI_RELEASE, 0}
    };
    for (size_t s = 0; s < sizeof(stages) / sizeof(stages[0]); ++s) {
        for (unsigned foreign = 0; foreign < 2; ++foreign) {
            CHECK(run_race(stages[s].kind, stages[s].skip, foreign != 0, false));
            if (stages[s].kind != SPI_RELEASE) {
                CHECK(run_race(stages[s].kind, stages[s].skip, foreign != 0, true));
            }
        }
    }
    return true;
}

static bool test_validation_timeout_and_network_failure(void)
{
    gs232b_fake_spi_bus_t fake;
    gs232b_shared_spi_t bus;
    CHECK(gs232b_fake_spi_bus_init(&fake, &bus));
    const gs232b_output_driver_t driver = gs232b_shared_spi_output_driver(&bus);
    CHECK(driver.apply(driver.ctx, 0xe0, 0) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(driver.apply(driver.ctx, 3, 0) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(driver.apply(driver.ctx, 12, 0) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(driver.apply(driver.ctx, 0, 1) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(driver.apply(driver.ctx, 4, 1001) == GS232B_ERROR_INVALID_ARGUMENT);
    uint8_t data = 1;
    CHECK(gs232b_shared_spi_w5500_transfer(&bus, 4, 0, &data, &data, 1) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_shared_spi_w5500_transfer(&bus, 4, 0, &data, NULL, 0) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_shared_spi_w5500_transfer(&bus, 4, 0, &data, NULL,
          GS232B_SHARED_SPI_MAX_TRANSFER + 1U) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_shared_spi_w5500_transfer(&bus, 0, 0, &data, NULL, 1) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_shared_spi_w5500_transfer(&bus, 5, 0, &data, NULL, 1) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(fake.count == 0);
    fake.lock_error = GS232B_ERROR_TIMEOUT;
    CHECK(driver.apply(driver.ctx, 4, 1000) == GS232B_ERROR_TIMEOUT);
    CHECK(fake.count == 0);
    CHECK(gs232b_shared_spi_set_ethernet_enabled(&bus, true) == GS232B_OK);
    fake.lock_error = GS232B_ERROR_TIMEOUT;
    CHECK(gs232b_shared_spi_w5500_transfer(&bus, 4, 0, &data, NULL, 1) ==
          GS232B_ERROR_TIMEOUT);
    CHECK(fake.count == 0);
    gs232b_fake_spi_bus_fault(&fake, SPI_ETHERNET, 0, GS232B_ERROR_TIMEOUT);
    CHECK(gs232b_shared_spi_w5500_transfer(&bus, 4, 0, &data, NULL, 1) ==
          GS232B_ERROR_TIMEOUT);
    CHECK(count_kind(&fake, SPI_LATCH_HIGH) == 0);
    CHECK(driver.apply(driver.ctx, 0x10, 0) == GS232B_OK);
    fake.lock_error = GS232B_ERROR_TIMEOUT;
    CHECK(gs232b_shared_spi_set_ethernet_enabled(&bus, false) == GS232B_ERROR_TIMEOUT);
    CHECK(gs232b_shared_spi_w5500_transfer(&bus, 4, 0, &data, NULL, 1) ==
          GS232B_ERROR_NOT_READY);
    CHECK(fake.latched == 0x10 && assert_atomic_trace(&fake));
    gs232b_fake_spi_bus_destroy(&fake);
    return true;
}

typedef struct {
    gs232b_shared_spi_t *bus;
    pthread_t closer;
    atomic_bool closed;
    bool started;
    bool draining;
    bool rejected;
    gs232b_error_t result;
} drain_t;

static void *close_gate(void *ctx)
{
    drain_t *drain = ctx;
    drain->result = gs232b_shared_spi_set_ethernet_enabled(drain->bus, false);
    atomic_store(&drain->closed, true);
    return NULL;
}

static void drain_hook(void *ctx, spi_event_kind_t event)
{
    drain_t *drain = ctx;
    if (event != SPI_ETHERNET || drain->started) { return; }
    drain->started = pthread_create(&drain->closer, NULL, close_gate, drain) == 0;
    if (!drain->started) { return; }
    while (atomic_load(&drain->bus->ethernet_enabled)) { sched_yield(); }
    /* NOTE: 新访问已禁止，关闭操作仍须等待当前真实在途事务返回。 */
    drain->draining = !atomic_load(&drain->closed);
    const uint8_t data = 0x55;
    drain->rejected = gs232b_shared_spi_w5500_transfer(
        drain->bus, 4, 0, &data, NULL, 1) == GS232B_ERROR_NOT_READY;
}

static bool test_gate_closes_before_draining_inflight_transfer(void)
{
    gs232b_fake_spi_bus_t fake;
    gs232b_shared_spi_t bus;
    CHECK(gs232b_fake_spi_bus_init(&fake, &bus));
    CHECK(gs232b_shared_spi_set_ethernet_enabled(&bus, true) == GS232B_OK);
    drain_t drain = {.bus = &bus};
    atomic_init(&drain.closed, false);
    fake.hook = drain_hook;
    fake.hook_ctx = &drain;
    const uint8_t data = 0xff;
    CHECK(gs232b_shared_spi_w5500_transfer(&bus, 4, 0, &data, NULL, 1) == GS232B_OK);
    CHECK(drain.started && pthread_join(drain.closer, NULL) == 0);
    CHECK(drain.draining && drain.rejected && atomic_load(&drain.closed) &&
          drain.result == GS232B_OK);
    CHECK(gs232b_shared_spi_w5500_transfer(&bus, 4, 0, &data, NULL, 1) ==
          GS232B_ERROR_NOT_READY);
    CHECK(count_kind(&fake, SPI_ETHERNET) == 1);
    const gs232b_output_driver_t driver = gs232b_shared_spi_output_driver(&bus);
    CHECK(driver.apply(driver.ctx, 0x10, 0) == GS232B_OK);
    CHECK(assert_atomic_trace(&fake));
    gs232b_fake_spi_bus_destroy(&fake);
    return true;
}

int main(void)
{
    if (!test_full_byte_after_ethernet() || !test_failures_release_without_extra_latch() ||
        !test_concurrent_transactions_at_every_boundary() ||
        !test_validation_timeout_and_network_failure() ||
        !test_gate_closes_before_draining_inflight_transfer()) { return 1; }
    puts("shared SPI trace, failure and concurrency tests passed");
    return 0;
}
