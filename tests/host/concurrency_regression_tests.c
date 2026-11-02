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

#include "concurrency_test_support.h"

int main(void)
{
    if (!concurrency_upload_tests()) return EXIT_FAILURE;
    if (!concurrency_pressure_tests()) return EXIT_FAILURE;
    if (!concurrency_fault_tests()) return EXIT_FAILURE;
    puts("Four WebSockets plus UART: bounded upload, pressure, epochs and faults passed");
    return EXIT_SUCCESS;
}
