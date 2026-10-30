#ifndef GS232B_TYPES_H
#define GS232B_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* Monotonic microseconds, never wall time. Zero is a valid timestamp. */
typedef uint64_t gs232b_time_us_t;
typedef uint64_t gs232b_command_epoch_t;

typedef enum {
    GS232B_OK = 0,
    GS232B_ERROR_INVALID_ARGUMENT = 1,
    GS232B_ERROR_BUSY = 2,
    GS232B_ERROR_NOT_OWNER = 3,
    GS232B_ERROR_NOT_AUTHORIZED = 4,
    GS232B_ERROR_NOT_READY = 5,
    GS232B_ERROR_CONFLICT = 6,
    GS232B_ERROR_TIMEOUT = 7,
    GS232B_ERROR_HARDWARE_FAULT = 8,
    GS232B_ERROR_IO = 9,
    GS232B_ERROR_NOT_FOUND = 10,
    GS232B_ERROR_WOULD_BLOCK = 11,
    GS232B_ERROR_RESOURCE_EXHAUSTED = 12,
    GS232B_ERROR_BUFFER_TOO_SMALL = 13,
    GS232B_ERROR_STALE_SESSION = 14,
    GS232B_ERROR_CLOSED = 15
} gs232b_error_t;

const char *gs232b_error_name(gs232b_error_t error);

typedef enum {
    GS232B_AXIS_AZIMUTH = 0,
    GS232B_AXIS_ELEVATION = 1,
    GS232B_AXIS_COUNT = 2
} gs232b_axis_id_t;

/* Both fields must be nonzero. A recycled session_id gets a new generation. */
typedef struct {
    uint32_t session_id;
    uint64_t generation;
} gs232b_session_key_t;

bool gs232b_session_key_valid(gs232b_session_key_t key);
bool gs232b_session_key_equal(gs232b_session_key_t left, gs232b_session_key_t right);

/* Stamp at input admission, including partial lines, before any queueing. */
typedef struct {
    gs232b_session_key_t session;
    gs232b_command_epoch_t epoch;
} gs232b_command_context_t;

bool gs232b_command_context_current(gs232b_command_context_t command,
                                    gs232b_session_key_t live_session,
                                    gs232b_command_epoch_t current_epoch);
/* Epoch starts at 1. Exhaustion fails without wrapping or modifying the value. */
gs232b_error_t gs232b_command_epoch_advance(gs232b_command_epoch_t *epoch);

typedef enum {
    GS232B_TRANSPORT_UART1 = 0,
    GS232B_TRANSPORT_ETHERNET_WS,
    GS232B_TRANSPORT_WIFI_WS,
    GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS
} gs232b_transport_t;

typedef struct {
    gs232b_time_us_t acquired_at_us;
    uint16_t raw;
    uint32_t adc_uv; /* Calibrated voltage at the ADC pin, before zero correction. */
    bool valid;
} gs232b_sample_t;

typedef enum {
    GS232B_AZ_STOP = 0,
    GS232B_AZ_CW,
    GS232B_AZ_CCW
} gs232b_az_direction_t;

typedef enum {
    GS232B_EL_STOP = 0,
    GS232B_EL_UP,
    GS232B_EL_DOWN
} gs232b_el_direction_t;

/* One complete logical output image. There is only one speed channel. */
typedef struct {
    gs232b_az_direction_t azimuth;
    gs232b_el_direction_t elevation;
    uint16_t speed_permille; /* 0..1000, azimuth only; not a raw LEDC count. */
    bool w5500_reset_asserted;
} gs232b_output_frame_t;

bool gs232b_output_frame_valid(const gs232b_output_frame_t *frame);

typedef struct {
    gs232b_output_frame_t requested;
    gs232b_output_frame_t last_successful;
    gs232b_time_us_t attempted_at_us;
    gs232b_error_t last_error;
    /* False initially and after a failed submission. A stop request is no proof
     * of a stopped output. True means submitted, not externally measured. */
    bool known;
} gs232b_output_snapshot_t;

void gs232b_output_snapshot_init(gs232b_output_snapshot_t *snapshot);

typedef enum {
    GS232B_AXIS_DISABLED = 0,
    GS232B_AXIS_IDLE,
    GS232B_AXIS_MANUAL,
    GS232B_AXIS_GOTO,
    GS232B_AXIS_REVERSAL_WAIT,
    GS232B_AXIS_FAULT
} gs232b_axis_mode_t;

typedef enum {
    GS232B_FAULT_NONE = 0,
    GS232B_FAULT_SAMPLE,
    GS232B_FAULT_OUTPUT,
    GS232B_FAULT_LIMIT,
    GS232B_FAULT_FEEDBACK_DIRECTION,
    GS232B_FAULT_NO_PROGRESS,
    GS232B_FAULT_RUN_TIMEOUT,
    GS232B_FAULT_SEQUENCE_LATE
} gs232b_fault_code_t;

typedef struct {
    gs232b_fault_code_t code;
    gs232b_error_t error;
    gs232b_time_us_t occurred_at_us;
    uint8_t axis_mask; /* bit0 AZ, bit1 EL; zero for a non-axis-specific fault. */
} gs232b_fault_snapshot_t;

typedef struct {
    gs232b_axis_id_t axis;
    gs232b_axis_mode_t mode;
    gs232b_sample_t sample;
    gs232b_error_t sample_error;
    int32_t position_mdeg; /* Mechanical travel, independent of P36/P45/Z. */
    int32_t target_mdeg;
    int32_t minimum_mdeg;
    int32_t maximum_mdeg;
    bool position_valid;
    bool target_valid;
    bool zero_valid;
    bool limits_valid;
} gs232b_axis_snapshot_t;

typedef enum {
    GS232B_SEQUENCE_EMPTY = 0,
    GS232B_SEQUENCE_LOADED_WAIT_T,
    GS232B_SEQUENCE_RUNNING,
    GS232B_SEQUENCE_COMPLETED
} gs232b_sequence_state_t;

typedef struct {
    gs232b_sequence_state_t state;
    uint16_t current_point; /* One-based; zero when empty. */
    uint16_t point_count;
    gs232b_time_us_t next_deadline_us; /* Meaningful only while running. */
} gs232b_sequence_snapshot_t;

typedef enum {
    GS232B_INTERACTION_NONE = 0,
    GS232B_INTERACTION_ZERO_AZ,
    GS232B_INTERACTION_ZERO_EL,
    GS232B_INTERACTION_DISPLAY_AZ,
    GS232B_INTERACTION_DISPLAY_BOTH
} gs232b_interaction_t;

typedef struct {
    gs232b_session_key_t key;
    gs232b_transport_t transport;
    bool connected;
    bool management_authorized;
    gs232b_interaction_t interaction;
    gs232b_time_us_t last_rx_at_us;
    gs232b_time_us_t last_tx_at_us;
    gs232b_error_t last_error;
} gs232b_session_snapshot_t;

/* Value objects, copied by the owner under its synchronization boundary.
 * No pointers to drivers, sockets, RTOS objects, or mutable shared storage. */
typedef struct {
    gs232b_time_us_t captured_at_us;
    gs232b_command_epoch_t command_epoch;
    gs232b_session_key_t owner; /* Invalid (all zero) when unowned. */
    gs232b_axis_snapshot_t axes[GS232B_AXIS_COUNT];
    gs232b_sequence_snapshot_t sequence;
    gs232b_output_snapshot_t output;
    gs232b_fault_snapshot_t fault;
} gs232b_state_snapshot_t;

/* Both axes disabled; no valid sample/zero/limits; output unknown; epoch 1. */
void gs232b_state_snapshot_init(gs232b_state_snapshot_t *snapshot);

#endif
