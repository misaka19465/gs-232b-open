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

#include "adc_api.h"
#include "freertos/queue.h"
#include "gs232b/types.h"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct fake_queue {
    pthread_mutex_t mutex;
    gs232b_sample_t item;
    bool allocated;
    bool full;
};

static struct fake_queue queues[2];

static void check_thread(int error)
{
    if (error != 0) { abort(); }
}

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    assert(length == 1 && item_size == sizeof(gs232b_sample_t));
    ++esp_adc_fake.queue_creates;
    if (esp_adc_fake.queue_creates == esp_adc_fake.fail_queue_create_at) {
        return NULL;
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (!queues[i].allocated) {
            queues[i].allocated = true;
            queues[i].full = false;
            check_thread(pthread_mutex_init(&queues[i].mutex, NULL));
            ++esp_adc_fake.queue_allocations;
            return &queues[i];
        }
    }
    assert(false);
    return NULL;
}

BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item)
{
    assert(queue != NULL && queue->allocated);
    ++esp_adc_fake.queue_overwrites;
    check_thread(pthread_mutex_lock(&queue->mutex));
    memcpy(&queue->item, item, sizeof(queue->item));
    queue->full = true;
    check_thread(pthread_mutex_unlock(&queue->mutex));
    return pdPASS;
}

BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t ticks_to_wait)
{
    assert(queue != NULL && queue->allocated && ticks_to_wait == 0);
    ++esp_adc_fake.queue_peeks;
    check_thread(pthread_mutex_lock(&queue->mutex));
    const BaseType_t result = queue->full ? pdPASS : pdFAIL;
    if (queue->full) { memcpy(item, &queue->item, sizeof(queue->item)); }
    check_thread(pthread_mutex_unlock(&queue->mutex));
    return result;
}

void vQueueDelete(QueueHandle_t queue)
{
    assert(queue != NULL && queue->allocated);
    check_thread(pthread_mutex_destroy(&queue->mutex));
    queue->allocated = false;
    ++esp_adc_fake.queue_deletes;
}
