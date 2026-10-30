#ifndef GS232B_PORTS_H
#define GS232B_PORTS_H

#include <stddef.h>
#include <stdint.h>

#include "gs232b/types.h"

/* Ports borrow ctx; their owner must outlive every copy of the port. Adapters
 * serialize access. Callbacks return promptly, with bounded work, and never
 * retain caller buffers. Store operations run outside the motion loop. */
typedef struct {
    void *ctx;
    gs232b_time_us_t (*now_us)(void *ctx);
} gs232b_clock_t;

typedef struct {
    void *ctx;
    gs232b_error_t (*read)(void *ctx, gs232b_axis_id_t axis, gs232b_sample_t *sample);
} gs232b_sampler_t;

/* Success supplies a valid sample; failure clears sample, including validity.
 * acquired_at_us is acquisition time, not the time a cached sample is read. */
gs232b_error_t gs232b_sampler_read(const gs232b_sampler_t *sampler,
                                  gs232b_axis_id_t axis, gs232b_sample_t *sample);

typedef struct {
    void *ctx;
    /* Apply the complete image through the single output manager. An error
     * means physical state may be unknown, including partial PWM/SPI updates.
     * Reversal timing and fault/stop policy belong to the motion layer. */
    gs232b_error_t (*apply)(void *ctx, const gs232b_output_frame_t *frame);
} gs232b_output_t;

gs232b_error_t gs232b_output_apply(const gs232b_output_t *output,
                                  const gs232b_output_frame_t *frame);

typedef enum {
    GS232B_STORE_NETWORK = 0,
    GS232B_STORE_DISPLAY,
    GS232B_STORE_ZERO_AZ,
    GS232B_STORE_ZERO_EL,
    GS232B_STORE_DOMAIN_COUNT
} gs232b_store_domain_t;

/* Opaque serialized records, one atomic replacement per logical domain.
 * The configuration layer encodes schema/revision/checksum; never persist a
 * native C struct. Success is durable; a failed save/erase preserves the old
 * record. A power loss must leave an intact old or new record. */
typedef struct {
    void *ctx;
    /* BUFFER_TOO_SMALL returns required size, with buffer untouched. Other
     * failures return size zero. NULL buffer with zero capacity is a probe. */
    gs232b_error_t (*load)(void *ctx, gs232b_store_domain_t domain,
                           uint8_t *buffer, size_t capacity, size_t *size);
    gs232b_error_t (*save)(void *ctx, gs232b_store_domain_t domain,
                           const uint8_t *record, size_t size);
    gs232b_error_t (*erase)(void *ctx, gs232b_store_domain_t domain);
} gs232b_store_t;

gs232b_error_t gs232b_store_load(const gs232b_store_t *store,
                                gs232b_store_domain_t domain,
                                uint8_t *buffer, size_t capacity, size_t *size);
gs232b_error_t gs232b_store_save(const gs232b_store_t *store,
                                gs232b_store_domain_t domain,
                                const uint8_t *record, size_t size);
gs232b_error_t gs232b_store_erase(const gs232b_store_t *store,
                                 gs232b_store_domain_t domain);

/* Byte streams: no text conversion, automatic CR, parsing, or broadcast.
 * Every operation validates the complete key against the live connection.
 * Receive returns a nonempty prefix or WOULD_BLOCK, never waits for input.
 * Send copies the entire span into a bounded per-session FIFO or returns an
 * error without accepting any bytes. Success means queued, not delivered.
 * Closing discards that connection's pending bytes. Lifecycle/owner-loss
 * events, stop priority, command admission, and epoch changes belong above
 * this port; a transport callback must not drive motion itself. */
typedef struct {
    void *ctx;
    gs232b_error_t (*receive)(void *ctx, gs232b_session_key_t session,
                              uint8_t *buffer, size_t capacity, size_t *size);
    gs232b_error_t (*send)(void *ctx, gs232b_session_key_t session,
                           const uint8_t *bytes, size_t size);
    gs232b_error_t (*close)(void *ctx, gs232b_session_key_t session);
} gs232b_session_io_t;

gs232b_error_t gs232b_session_receive(const gs232b_session_io_t *io,
                                     gs232b_session_key_t session,
                                     uint8_t *buffer, size_t capacity, size_t *size);
gs232b_error_t gs232b_session_send(const gs232b_session_io_t *io,
                                  gs232b_session_key_t session,
                                  const uint8_t *bytes, size_t size);
gs232b_error_t gs232b_session_close(const gs232b_session_io_t *io,
                                   gs232b_session_key_t session);

#endif
