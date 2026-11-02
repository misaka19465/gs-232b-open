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

#ifndef GS232B_FAKE_SPI_BUS_H
#define GS232B_FAKE_SPI_BUS_H

#include <pthread.h>
#include <stdatomic.h>

#include "gs232b/shared_spi.h"

typedef enum {
    SPI_ACQUIRE = 0,
    SPI_ACQUIRE_FAILED,
    SPI_SHIFT_BYTE,
    SPI_COMPLETE,
    SPI_LATCH_LOW,
    SPI_LATCH_HIGH,
    SPI_RELEASE,
    SPI_ETHERNET,
    SPI_PWM,
    SPI_QUIESCE,
    SPI_RESUME,
    SPI_EVENT_COUNT
} spi_event_kind_t;

typedef struct {
    spi_event_kind_t kind;
    uint32_t value;
    uint16_t address;
    size_t size;
} spi_event_t;

#define SPI_TRACE_CAPACITY 16384U

typedef struct gs232b_fake_spi_bus {
    pthread_mutex_t entry;
    pthread_mutex_t host;
    pthread_mutex_t trace_mutex;
    spi_event_t trace[SPI_TRACE_CAPACITY];
    size_t count;
    atomic_uint_fast64_t now_us;
    atomic_bool safe;
    gs232b_time_us_t latency_us;
    uint8_t shifted;
    uint8_t latched;
    uint16_t pwm;
    bool latch_high;
    bool owned;
    bool complete;
    gs232b_error_t lock_error;
    spi_event_kind_t fault_kind;
    unsigned fault_skip;
    gs232b_error_t fault_error;
    void *hook_ctx;
    void (*hook)(void *ctx, spi_event_kind_t event);
} gs232b_fake_spi_bus_t;

bool gs232b_fake_spi_bus_init(gs232b_fake_spi_bus_t *fake, gs232b_shared_spi_t *bus);
void gs232b_fake_spi_bus_destroy(gs232b_fake_spi_bus_t *fake);
void gs232b_fake_spi_bus_clear(gs232b_fake_spi_bus_t *fake);
void gs232b_fake_spi_bus_fault(gs232b_fake_spi_bus_t *fake, spi_event_kind_t kind,
                               unsigned skip, gs232b_error_t error);
gs232b_error_t gs232b_fake_spi_bus_event(gs232b_fake_spi_bus_t *fake,
                                        spi_event_kind_t kind, uint32_t value);
gs232b_error_t gs232b_fake_spi_bus_foreign_transfer(gs232b_fake_spi_bus_t *fake);
gs232b_time_us_t gs232b_fake_spi_bus_now(void *ctx);

#endif
