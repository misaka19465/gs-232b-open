#ifndef GS232B_FAKES_H
#define GS232B_FAKES_H

#include "gs232b/ports.h"

/* Test-only, allocation-free and single-threaded. Instances own their state;
 * there are no globals, real timers, sleeps, ESP-IDF headers, or peripherals. */
#define GS232B_FAKE_OUTPUT_TRACE_CAPACITY 256
#define GS232B_FAKE_STORE_RECORD_CAPACITY 2048
#define GS232B_FAKE_SESSION_CAPACITY 7
#define GS232B_FAKE_SESSION_BUFFER_CAPACITY 4096

typedef struct {
    gs232b_error_t error;
    size_t remaining;
} gs232b_fake_failure_t;

/* The next count valid attempts fail, then normal behavior resumes. Invalid
 * calls and capacity checks do not consume an injected failure. */
gs232b_error_t gs232b_fake_fail_next(gs232b_fake_failure_t *failure,
                                    gs232b_error_t error, size_t count);

typedef struct {
    gs232b_time_us_t now_us;
} gs232b_fake_clock_t;

void gs232b_fake_clock_init(gs232b_fake_clock_t *fake, gs232b_time_us_t start_us);
gs232b_clock_t gs232b_fake_clock_port(gs232b_fake_clock_t *fake);
/* Rejects overflow without moving time backward. */
gs232b_error_t gs232b_fake_clock_advance(gs232b_fake_clock_t *fake,
                                        gs232b_time_us_t delta_us);

typedef struct {
    gs232b_clock_t clock;
    gs232b_sample_t samples[GS232B_AXIS_COUNT];
    gs232b_fake_failure_t failures[GS232B_AXIS_COUNT];
    size_t read_count[GS232B_AXIS_COUNT];
} gs232b_fake_sampler_t;

gs232b_error_t gs232b_fake_sampler_init(gs232b_fake_sampler_t *fake,
                                       gs232b_clock_t clock);
gs232b_sampler_t gs232b_fake_sampler_port(gs232b_fake_sampler_t *fake);
/* Latches one sample at the current virtual time. Reads do not refresh it. */
gs232b_error_t gs232b_fake_sampler_set(gs232b_fake_sampler_t *fake,
                                      gs232b_axis_id_t axis, uint16_t raw,
                                      uint32_t adc_uv);

typedef struct {
    gs232b_time_us_t at_us;
    gs232b_output_frame_t frame;
    gs232b_error_t result;
} gs232b_fake_output_event_t;

typedef struct {
    gs232b_clock_t clock;
    gs232b_output_snapshot_t snapshot;
    gs232b_fake_failure_t failure;
    gs232b_fake_output_event_t trace[GS232B_FAKE_OUTPUT_TRACE_CAPACITY];
    size_t trace_count;
} gs232b_fake_output_t;

gs232b_error_t gs232b_fake_output_init(gs232b_fake_output_t *fake,
                                      gs232b_clock_t clock);
gs232b_output_t gs232b_fake_output_port(gs232b_fake_output_t *fake);
/* Trace contains successful and injected-failure attempts, in call order.
 * A full trace refuses further submissions with RESOURCE_EXHAUSTED; it never
 * overwrites history. Clear it explicitly between observation windows. */
void gs232b_fake_output_clear_trace(gs232b_fake_output_t *fake);

typedef enum {
    GS232B_FAKE_STORE_LOAD = 0,
    GS232B_FAKE_STORE_SAVE,
    GS232B_FAKE_STORE_ERASE,
    GS232B_FAKE_STORE_OPERATION_COUNT
} gs232b_fake_store_operation_t;

typedef struct {
    uint8_t bytes[GS232B_FAKE_STORE_RECORD_CAPACITY];
    size_t size;
    bool present;
} gs232b_fake_store_record_t;

typedef struct {
    gs232b_fake_store_record_t records[GS232B_STORE_DOMAIN_COUNT];
    gs232b_fake_failure_t failures[GS232B_STORE_DOMAIN_COUNT]
                                    [GS232B_FAKE_STORE_OPERATION_COUNT];
} gs232b_fake_store_t;

void gs232b_fake_store_init(gs232b_fake_store_t *fake);
gs232b_store_t gs232b_fake_store_port(gs232b_fake_store_t *fake);

typedef enum {
    GS232B_FAKE_SESSION_RECEIVE = 0,
    GS232B_FAKE_SESSION_SEND,
    GS232B_FAKE_SESSION_OPERATION_COUNT
} gs232b_fake_session_operation_t;

typedef struct {
    uint8_t bytes[GS232B_FAKE_SESSION_BUFFER_CAPACITY];
    size_t size;
} gs232b_fake_byte_queue_t;

typedef struct {
    gs232b_session_snapshot_t snapshot;
    gs232b_fake_byte_queue_t rx;
    gs232b_fake_byte_queue_t tx;
    gs232b_fake_failure_t failures[GS232B_FAKE_SESSION_OPERATION_COUNT];
} gs232b_fake_session_slot_t;

typedef struct {
    gs232b_clock_t clock;
    gs232b_fake_session_slot_t slots[GS232B_FAKE_SESSION_CAPACITY];
} gs232b_fake_sessions_t;

gs232b_error_t gs232b_fake_sessions_init(gs232b_fake_sessions_t *fake,
                                        gs232b_clock_t clock);
gs232b_session_io_t gs232b_fake_sessions_port(gs232b_fake_sessions_t *fake);
gs232b_error_t gs232b_fake_sessions_open(gs232b_fake_sessions_t *fake,
                                        gs232b_transport_t transport,
                                        gs232b_session_key_t *session);
/* Feed inbound bytes or drain accepted outbound bytes, preserving all CR/LF,
 * NUL and fragmentation. Full queues return WOULD_BLOCK without partial writes.
 * Empty reads return WOULD_BLOCK with size zero. */
gs232b_error_t gs232b_fake_sessions_feed(gs232b_fake_sessions_t *fake,
                                        gs232b_session_key_t session,
                                        const uint8_t *bytes, size_t size);
gs232b_error_t gs232b_fake_sessions_take_tx(gs232b_fake_sessions_t *fake,
                                           gs232b_session_key_t session,
                                           uint8_t *buffer, size_t capacity,
                                           size_t *size);
gs232b_error_t gs232b_fake_sessions_fail_next(gs232b_fake_sessions_t *fake,
                                             gs232b_session_key_t session,
                                             gs232b_fake_session_operation_t operation,
                                             gs232b_error_t error, size_t count);
/* Also permits inspecting a closed slot until it is reused. */
gs232b_error_t gs232b_fake_sessions_snapshot(gs232b_fake_sessions_t *fake,
                                            gs232b_session_key_t session,
                                            gs232b_session_snapshot_t *snapshot);

#endif
