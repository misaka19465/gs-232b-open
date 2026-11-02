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

#include "gs232b/uart_baud.h"
#include "gs232b/fakes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static bool test_round_trip_and_atomicity(void)
{
    gs232b_fake_store_t fake;
    gs232b_fake_store_init(&fake);
    const gs232b_store_t store = gs232b_fake_store_port(&fake);
    uint32_t baud;
    uint64_t revision;
    CHECK(gs232b_uart_baud_load(&store, &baud, &revision) == GS232B_ERROR_NOT_FOUND);
    CHECK(baud == 9600 && revision == 0);
    uint64_t next;
    CHECK(gs232b_uart_baud_commit(&store, 0, 115200, &next) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(next == 0 && !fake.records[GS232B_STORE_UART_BAUD].present);
    CHECK(gs232b_uart_baud_commit(&store, 0, 1200, &revision) == GS232B_OK && revision == 1);
    CHECK(gs232b_uart_baud_commit(&store, 0, 4800, &next) == GS232B_ERROR_CONFLICT);
    CHECK(gs232b_fake_fail_next(&fake.failures[GS232B_STORE_UART_BAUD][GS232B_FAKE_STORE_INTERRUPT],
        GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs232b_uart_baud_commit(&store, revision, 4800, &next) == GS232B_ERROR_IO);
    CHECK(gs232b_uart_baud_load(&store, &baud, &revision) == GS232B_OK && baud == 1200 && revision == 1);
    gs232b_fake_store_record_t others[GS232B_STORE_UART_BAUD];
    memcpy(others, fake.records, sizeof(others));
    const uint32_t rates[] = {2400, 4800, 9600};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        CHECK(gs232b_uart_baud_commit(&store, revision, rates[i], &next) == GS232B_OK);
        CHECK(gs232b_uart_baud_load(&store, &baud, &revision) == GS232B_OK);
        CHECK(baud == rates[i] && revision == next && revision == i + 2);
    }
    CHECK(memcmp(others, fake.records, sizeof(others)) == 0);
    return true;
}

static bool test_corrupt_records_and_io(void)
{
    for (unsigned fault = 0; fault < 4; ++fault) {
        gs232b_fake_store_t fake;
        gs232b_fake_store_init(&fake);
        const gs232b_store_t store = gs232b_fake_store_port(&fake);
        uint64_t revision;
        uint32_t baud;
        CHECK(gs232b_uart_baud_commit(&store, 0, 2400, &revision) == GS232B_OK);
        if (fault == 0) {
            CHECK(gs232b_uart_baud_commit(&store, revision, 9600, &revision) == GS232B_OK);
            fake.records[GS232B_STORE_UART_BAUD].bytes[18] ^= 0xff; /* CRC 错误 */
        } else if (fault == 1) {
            const uint8_t unsupported[] = {0, 0xc2, 1, 0}; /* CRC 正确的 115200 */
            CHECK(gs232b_persistence_commit(&store, GS232B_STORE_UART_BAUD, revision,
                1, unsupported, sizeof(unsupported), &revision) == GS232B_OK);
        } else if (fault == 2) {
            const uint8_t short_payload[] = {0x80, 0x25};
            CHECK(gs232b_persistence_commit(&store, GS232B_STORE_UART_BAUD, revision,
                1, short_payload, sizeof(short_payload), &revision) == GS232B_OK);
        } else {
            /* NOTE: 只创建 schema=2 的最新槽，上一 schema=1 必须仍可恢复。 */
            gs232b_fake_store_t newer;
            gs232b_fake_store_init(&newer);
            const gs232b_store_t newer_store = gs232b_fake_store_port(&newer);
            const uint8_t future[] = {0x80, 0x25, 0, 0};
            CHECK(gs232b_persistence_commit(&newer_store, GS232B_STORE_UART_BAUD, 0,
                2, future, sizeof(future), &revision) == GS232B_OK);
            const gs232b_fake_store_record_t *record = &newer.records[GS232B_STORE_UART_BAUD];
            CHECK(gs232b_store_save(&store, GS232B_STORE_UART_BAUD, record->bytes,
                record->size) == GS232B_OK);
        }
        CHECK(gs232b_uart_baud_load(&store, &baud, &revision) == GS232B_OK);
        CHECK(baud == 2400 && revision == 1);
        memset(&fake.records[GS232B_STORE_UART_BAUD], 0, sizeof(fake.records[0]));
        fake.records[GS232B_STORE_UART_BAUD].present = true;
        CHECK(gs232b_uart_baud_load(&store, &baud, &revision) == GS232B_ERROR_CORRUPT);
        CHECK(baud == 9600 && revision == 0);
    }
    gs232b_fake_store_t fake;
    gs232b_fake_store_init(&fake);
    const gs232b_store_t store = gs232b_fake_store_port(&fake);
    uint32_t baud;
    uint64_t revision;
    CHECK(gs232b_fake_fail_next(&fake.failures[GS232B_STORE_UART_BAUD][GS232B_FAKE_STORE_LOAD],
        GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs232b_uart_baud_load(&store, &baud, &revision) == GS232B_ERROR_IO);
    CHECK(baud == 9600 && revision == 0);
    return true;
}

int main(void)
{
    return test_round_trip_and_atomicity() && test_corrupt_records_and_io() ? EXIT_SUCCESS : EXIT_FAILURE;
}
