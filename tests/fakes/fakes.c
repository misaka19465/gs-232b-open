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

#include "gs232b/fakes.h"

#include <string.h>

static bool axis_valid(gs232b_axis_id_t axis)
{
    return axis >= GS232B_AXIS_AZIMUTH && axis < GS232B_AXIS_COUNT;
}

static bool domain_valid(gs232b_store_domain_t domain)
{
    return domain >= GS232B_STORE_NETWORK && domain < GS232B_STORE_DOMAIN_COUNT;
}

static gs232b_error_t consume_failure(gs232b_fake_failure_t *failure)
{
    if (failure->remaining == 0) {
        return GS232B_OK;
    }
    --failure->remaining;
    return failure->error;
}

gs232b_error_t gs232b_fake_fail_next(gs232b_fake_failure_t *failure,
                                    gs232b_error_t error, size_t count)
{
    if (failure == NULL || error <= GS232B_OK || error > GS232B_ERROR_CORRUPT || count == 0) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    failure->error = error;
    failure->remaining = count;
    return GS232B_OK;
}

static gs232b_time_us_t clock_now(void *ctx)
{
    return ((gs232b_fake_clock_t *)ctx)->now_us;
}

void gs232b_fake_clock_init(gs232b_fake_clock_t *fake, gs232b_time_us_t start_us)
{
    if (fake != NULL) {
        fake->now_us = start_us;
    }
}

gs232b_clock_t gs232b_fake_clock_port(gs232b_fake_clock_t *fake)
{
    return (gs232b_clock_t){.ctx = fake, .now_us = clock_now};
}

gs232b_error_t gs232b_fake_clock_advance(gs232b_fake_clock_t *fake,
                                        gs232b_time_us_t delta_us)
{
    if (fake == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (delta_us > UINT64_MAX - fake->now_us) {
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    fake->now_us += delta_us;
    return GS232B_OK;
}

static gs232b_error_t sampler_read(void *ctx, gs232b_axis_id_t axis,
                                  gs232b_sample_t *sample)
{
    gs232b_fake_sampler_t *fake = ctx;
    if (sample == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(sample, 0, sizeof(*sample));
    if (fake == NULL || !axis_valid(axis)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    ++fake->read_count[axis];
    gs232b_error_t error = consume_failure(&fake->failures[axis]);
    if (error != GS232B_OK) {
        return error;
    }
    if (!fake->samples[axis].valid) {
        return GS232B_ERROR_NOT_READY;
    }
    *sample = fake->samples[axis];
    return GS232B_OK;
}

gs232b_error_t gs232b_fake_sampler_init(gs232b_fake_sampler_t *fake,
                                       gs232b_clock_t clock)
{
    if (fake == NULL || clock.now_us == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(fake, 0, sizeof(*fake));
    fake->clock = clock;
    return GS232B_OK;
}

gs232b_sampler_t gs232b_fake_sampler_port(gs232b_fake_sampler_t *fake)
{
    return (gs232b_sampler_t){.ctx = fake, .read = sampler_read};
}

gs232b_error_t gs232b_fake_sampler_set(gs232b_fake_sampler_t *fake,
                                      gs232b_axis_id_t axis, uint16_t raw,
                                      uint32_t adc_uv)
{
    if (fake == NULL || fake->clock.now_us == NULL || !axis_valid(axis)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    fake->samples[axis] = (gs232b_sample_t){
        .acquired_at_us = fake->clock.now_us(fake->clock.ctx),
        .raw = raw,
        .adc_uv = adc_uv,
        .valid = true
    };
    return GS232B_OK;
}

static gs232b_error_t output_apply(void *ctx, const gs232b_output_frame_t *frame)
{
    gs232b_fake_output_t *fake = ctx;
    if (fake == NULL || !gs232b_output_frame_valid(frame)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    fake->snapshot.requested = *frame;
    fake->snapshot.attempted_at_us = fake->clock.now_us(fake->clock.ctx);
    gs232b_error_t error;
    if (fake->trace_count == GS232B_FAKE_OUTPUT_TRACE_CAPACITY) {
        error = GS232B_ERROR_RESOURCE_EXHAUSTED;
    } else {
        error = consume_failure(&fake->failure);
        fake->trace[fake->trace_count++] = (gs232b_fake_output_event_t){
            .at_us = fake->snapshot.attempted_at_us,
            .frame = *frame,
            .result = error
        };
    }
    fake->snapshot.last_error = error;
    fake->snapshot.known = error == GS232B_OK;
    if (error == GS232B_OK) {
        fake->snapshot.last_successful = *frame;
    }
    return error;
}

gs232b_error_t gs232b_fake_output_init(gs232b_fake_output_t *fake,
                                      gs232b_clock_t clock)
{
    if (fake == NULL || clock.now_us == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(fake, 0, sizeof(*fake));
    fake->clock = clock;
    gs232b_output_snapshot_init(&fake->snapshot);
    return GS232B_OK;
}

gs232b_output_t gs232b_fake_output_port(gs232b_fake_output_t *fake)
{
    return (gs232b_output_t){.ctx = fake, .apply = output_apply};
}

void gs232b_fake_output_clear_trace(gs232b_fake_output_t *fake)
{
    if (fake != NULL) {
        fake->trace_count = 0;
    }
}

static gs232b_error_t store_load(void *ctx, gs232b_store_domain_t domain,
                                uint8_t *buffer, size_t capacity, size_t *size)
{
    gs232b_fake_store_t *fake = ctx;
    if (size == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *size = 0;
    if (fake == NULL || !domain_valid(domain) || (capacity != 0 && buffer == NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_error_t error = consume_failure(&fake->failures[domain][GS232B_FAKE_STORE_LOAD]);
    if (error != GS232B_OK) {
        return error;
    }
    const gs232b_fake_store_record_t *record = &fake->records[domain];
    if (!record->present) {
        return GS232B_ERROR_NOT_FOUND;
    }
    *size = record->size;
    if (record->size > capacity) {
        return GS232B_ERROR_BUFFER_TOO_SMALL;
    }
    if (record->size != 0) {
        memmove(buffer, record->bytes, record->size);
    }
    return GS232B_OK;
}

static gs232b_error_t store_load_previous(void *ctx, gs232b_store_domain_t domain,
                                         uint8_t *buffer, size_t capacity, size_t *size)
{
    gs232b_fake_store_t *fake = ctx;
    if (size == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *size = 0;
    if (fake == NULL || !domain_valid(domain) || (capacity != 0 && buffer == NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_error_t error = consume_failure(
        &fake->failures[domain][GS232B_FAKE_STORE_LOAD]);
    if (error != GS232B_OK) {
        return error;
    }
    const gs232b_fake_store_record_t *record = &fake->records[domain];
    if (!record->previous_present) {
        return GS232B_ERROR_NOT_FOUND;
    }
    *size = record->previous_size;
    if (record->previous_size > capacity) {
        return GS232B_ERROR_BUFFER_TOO_SMALL;
    }
    if (record->previous_size != 0) {
        memmove(buffer, record->previous_bytes, record->previous_size);
    }
    return GS232B_OK;
}

static gs232b_error_t store_save(void *ctx, gs232b_store_domain_t domain,
                                const uint8_t *bytes, size_t size)
{
    gs232b_fake_store_t *fake = ctx;
    if (fake == NULL || !domain_valid(domain) || (size != 0 && bytes == NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (size > GS232B_FAKE_STORE_RECORD_CAPACITY) {
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    gs232b_error_t error = consume_failure(&fake->failures[domain][GS232B_FAKE_STORE_SAVE]);
    if (error != GS232B_OK) {
        return error;
    }
    gs232b_fake_store_record_t *record = &fake->records[domain];
    if (record->present) {
        memmove(record->previous_bytes, record->bytes, record->size);
        record->previous_size = record->size;
        record->previous_present = true;
    }
    if (size != 0) {
        memmove(record->bytes, bytes, size);
    }
    record->size = size;
    record->present = true;
    error = consume_failure(&fake->failures[domain][GS232B_FAKE_STORE_INTERRUPT]);
    if (error != GS232B_OK) {
        /* NOTE: 未激活槽可能已写入，但选择标记未切换；恢复活动镜像，确保
         * 调用方看到的仍是上一个完整版本。 */
        if (record->previous_present) {
            memmove(record->bytes, record->previous_bytes, record->previous_size);
            record->size = record->previous_size;
            record->present = true;
        } else {
            record->size = 0;
            record->present = false;
        }
        return error;
    }
    return GS232B_OK;
}

static gs232b_error_t store_erase(void *ctx, gs232b_store_domain_t domain)
{
    gs232b_fake_store_t *fake = ctx;
    if (fake == NULL || !domain_valid(domain)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_error_t error = consume_failure(&fake->failures[domain][GS232B_FAKE_STORE_ERASE]);
    if (error != GS232B_OK) {
        return error;
    }
    memset(&fake->records[domain], 0, sizeof(fake->records[domain]));
    return GS232B_OK;
}

void gs232b_fake_store_init(gs232b_fake_store_t *fake)
{
    if (fake != NULL) {
        memset(fake, 0, sizeof(*fake));
    }
}

gs232b_store_t gs232b_fake_store_port(gs232b_fake_store_t *fake)
{
    return (gs232b_store_t){.ctx = fake, .load = store_load,
                           .load_previous = store_load_previous,
                           .save = store_save, .erase = store_erase};
}

static gs232b_error_t find_slot(gs232b_fake_sessions_t *fake,
                               gs232b_session_key_t session,
                               gs232b_fake_session_slot_t **slot)
{
    if (fake == NULL || !gs232b_session_key_valid(session)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (session.session_id > GS232B_FAKE_SESSION_CAPACITY) {
        return GS232B_ERROR_STALE_SESSION;
    }
    *slot = &fake->slots[session.session_id - 1];
    if (!gs232b_session_key_equal((*slot)->snapshot.key, session)) {
        return GS232B_ERROR_STALE_SESSION;
    }
    return GS232B_OK;
}

static gs232b_error_t find_live_slot(gs232b_fake_sessions_t *fake,
                                    gs232b_session_key_t session,
                                    gs232b_fake_session_slot_t **slot)
{
    gs232b_error_t error = find_slot(fake, session, slot);
    if (error != GS232B_OK) {
        return error;
    }
    return (*slot)->snapshot.connected ? GS232B_OK : GS232B_ERROR_CLOSED;
}

static gs232b_error_t queue_push(gs232b_fake_byte_queue_t *queue,
                                const uint8_t *bytes, size_t size)
{
    if (size > GS232B_FAKE_SESSION_BUFFER_CAPACITY - queue->size) {
        return GS232B_ERROR_WOULD_BLOCK;
    }
    if (size != 0) {
        memmove(queue->bytes + queue->size, bytes, size);
    }
    queue->size += size;
    return GS232B_OK;
}

static gs232b_error_t queue_take(gs232b_fake_byte_queue_t *queue,
                                uint8_t *buffer, size_t capacity, size_t *size)
{
    if (queue->size == 0) {
        return GS232B_ERROR_WOULD_BLOCK;
    }
    *size = queue->size < capacity ? queue->size : capacity;
    memmove(buffer, queue->bytes, *size);
    queue->size -= *size;
    memmove(queue->bytes, queue->bytes + *size, queue->size);
    return GS232B_OK;
}

static gs232b_error_t session_receive(void *ctx, gs232b_session_key_t session,
                                     uint8_t *buffer, size_t capacity, size_t *size)
{
    if (size == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *size = 0;
    if (buffer == NULL || capacity == 0) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_fake_sessions_t *fake = ctx;
    gs232b_fake_session_slot_t *slot;
    gs232b_error_t error = find_live_slot(fake, session, &slot);
    if (error != GS232B_OK) {
        return error;
    }
    error = consume_failure(&slot->failures[GS232B_FAKE_SESSION_RECEIVE]);
    if (error == GS232B_OK) {
        error = queue_take(&slot->rx, buffer, capacity, size);
    }
    slot->snapshot.last_error = error;
    if (error == GS232B_OK) {
        slot->snapshot.last_rx_at_us = fake->clock.now_us(fake->clock.ctx);
    }
    return error;
}

static gs232b_error_t session_send(void *ctx, gs232b_session_key_t session,
                                  const uint8_t *bytes, size_t size)
{
    if (size != 0 && bytes == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_fake_sessions_t *fake = ctx;
    gs232b_fake_session_slot_t *slot;
    gs232b_error_t error = find_live_slot(fake, session, &slot);
    if (error != GS232B_OK) {
        return error;
    }
    if (size > GS232B_FAKE_SESSION_BUFFER_CAPACITY - slot->tx.size) {
        error = GS232B_ERROR_WOULD_BLOCK;
    } else {
        error = consume_failure(&slot->failures[GS232B_FAKE_SESSION_SEND]);
        if (error == GS232B_OK) {
            error = queue_push(&slot->tx, bytes, size);
        }
    }
    slot->snapshot.last_error = error;
    if (error == GS232B_OK) {
        slot->snapshot.last_tx_at_us = fake->clock.now_us(fake->clock.ctx);
    }
    return error;
}

static gs232b_error_t session_close(void *ctx, gs232b_session_key_t session)
{
    gs232b_fake_session_slot_t *slot;
    gs232b_error_t error = find_live_slot(ctx, session, &slot);
    if (error != GS232B_OK) {
        return error;
    }
    slot->snapshot.connected = false;
    slot->snapshot.management_authorized = false;
    slot->snapshot.interaction = GS232B_INTERACTION_NONE;
    slot->snapshot.last_error = GS232B_ERROR_CLOSED;
    slot->rx.size = 0;
    slot->tx.size = 0;
    memset(slot->failures, 0, sizeof(slot->failures));
    return GS232B_OK;
}

gs232b_error_t gs232b_fake_sessions_init(gs232b_fake_sessions_t *fake,
                                        gs232b_clock_t clock)
{
    if (fake == NULL || clock.now_us == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(fake, 0, sizeof(*fake));
    fake->clock = clock;
    return GS232B_OK;
}

gs232b_session_io_t gs232b_fake_sessions_port(gs232b_fake_sessions_t *fake)
{
    return (gs232b_session_io_t){.ctx = fake, .receive = session_receive,
                                .send = session_send, .close = session_close};
}

gs232b_error_t gs232b_fake_sessions_open(gs232b_fake_sessions_t *fake,
                                        gs232b_transport_t transport,
                                        gs232b_session_key_t *session)
{
    if (session == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *session = (gs232b_session_key_t){0};
    if (fake == NULL || fake->clock.now_us == NULL ||
        transport < GS232B_TRANSPORT_UART1 || transport > GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < GS232B_FAKE_SESSION_CAPACITY; ++index) {
        gs232b_fake_session_slot_t *slot = &fake->slots[index];
        if (slot->snapshot.connected || slot->snapshot.key.generation == UINT64_MAX) {
            continue;
        }
        uint64_t generation = slot->snapshot.key.generation + 1;
        memset(slot, 0, sizeof(*slot));
        slot->snapshot.key = (gs232b_session_key_t){
            .session_id = (uint32_t)index + 1, .generation = generation
        };
        slot->snapshot.transport = transport;
        slot->snapshot.connected = true;
        *session = slot->snapshot.key;
        return GS232B_OK;
    }
    return GS232B_ERROR_RESOURCE_EXHAUSTED;
}

gs232b_error_t gs232b_fake_sessions_feed(gs232b_fake_sessions_t *fake,
                                        gs232b_session_key_t session,
                                        const uint8_t *bytes, size_t size)
{
    if (size != 0 && bytes == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_fake_session_slot_t *slot;
    gs232b_error_t error = find_live_slot(fake, session, &slot);
    return error == GS232B_OK ? queue_push(&slot->rx, bytes, size) : error;
}

gs232b_error_t gs232b_fake_sessions_take_tx(gs232b_fake_sessions_t *fake,
                                           gs232b_session_key_t session,
                                           uint8_t *buffer, size_t capacity,
                                           size_t *size)
{
    if (size == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *size = 0;
    if (buffer == NULL || capacity == 0) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_fake_session_slot_t *slot;
    gs232b_error_t error = find_live_slot(fake, session, &slot);
    return error == GS232B_OK ? queue_take(&slot->tx, buffer, capacity, size) : error;
}

gs232b_error_t gs232b_fake_sessions_fail_next(gs232b_fake_sessions_t *fake,
                                             gs232b_session_key_t session,
                                             gs232b_fake_session_operation_t operation,
                                             gs232b_error_t error, size_t count)
{
    if (operation < GS232B_FAKE_SESSION_RECEIVE ||
        operation >= GS232B_FAKE_SESSION_OPERATION_COUNT) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_fake_session_slot_t *slot;
    gs232b_error_t lookup_error = find_live_slot(fake, session, &slot);
    if (lookup_error != GS232B_OK) {
        return lookup_error;
    }
    return gs232b_fake_fail_next(&slot->failures[operation], error, count);
}

gs232b_error_t gs232b_fake_sessions_snapshot(gs232b_fake_sessions_t *fake,
                                            gs232b_session_key_t session,
                                            gs232b_session_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    gs232b_fake_session_slot_t *slot;
    gs232b_error_t error = find_slot(fake, session, &slot);
    if (error == GS232B_OK) {
        *snapshot = slot->snapshot;
    }
    return error;
}
