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

#include "ws_api.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <stdlib.h>

struct fake_semaphore { bool binary, held, ready; };

static SemaphoreHandle_t create(bool binary)
{
    if (++fake_ws.allocations == fake_ws.fail_allocation) { return NULL; }
    SemaphoreHandle_t semaphore = calloc(1, sizeof(*semaphore));
    assert(semaphore != NULL);
    semaphore->binary = binary;
    ++fake_ws.live_semaphores;
    return semaphore;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return create(false); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return create(true); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks)
{
    if (semaphore->binary) {
        if (!semaphore->ready && ticks != 0 && fake_ws.control_step != NULL) {
            fake_ws.control_step(fake_ws.control_ctx);
        }
        const bool ready = semaphore->ready;
        semaphore->ready = false;
        return ready ? pdPASS : pdFAIL;
    }
    assert(ticks == 0 || !semaphore->held);
    if (semaphore->held) { return pdFAIL; }
    semaphore->held = true;
    return pdPASS;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    if (semaphore->binary) { semaphore->ready = true; }
    else { assert(semaphore->held); semaphore->held = false; }
    return pdPASS;
}
void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    assert(!semaphore->held && fake_ws.live_semaphores != 0);
    --fake_ws.live_semaphores;
    free(semaphore);
}
