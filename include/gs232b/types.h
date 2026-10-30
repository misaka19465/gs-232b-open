#ifndef GS232B_TYPES_H
#define GS232B_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* NOTE: 使用单调微秒计时，不使用墙上时钟；0 是有效时间戳。 */
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

/* NOTE: 两个字段都必须非零；复用 session_id 时必须使用新的 generation。 */
typedef struct {
    uint32_t session_id;
    uint64_t generation;
} gs232b_session_key_t;

bool gs232b_session_key_valid(gs232b_session_key_t key);
bool gs232b_session_key_equal(gs232b_session_key_t left, gs232b_session_key_t right);

/* NOTE: 在输入接纳时加盖时间戳，包括进入任何队列前的半行。 */
typedef struct {
    gs232b_session_key_t session;
    gs232b_command_epoch_t epoch;
} gs232b_command_context_t;

bool gs232b_command_context_current(gs232b_command_context_t command,
                                    gs232b_session_key_t live_session,
                                    gs232b_command_epoch_t current_epoch);
/* NOTE: epoch 从 1 开始；耗尽时返回失败，不回绕且不修改原值。 */
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
    uint32_t adc_uv; /* NOTE: ADC 引脚校准电压，尚未扣除零点修正。 */
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

/* NOTE: 一个完整的逻辑输出镜像；速度通道只有一个。 */
typedef struct {
    gs232b_az_direction_t azimuth;
    gs232b_el_direction_t elevation;
    uint16_t speed_permille; /* NOTE: 范围为 0..1000，仅用于方位；不是原始 LEDC 计数值。 */
    bool w5500_reset_asserted;
} gs232b_output_frame_t;

bool gs232b_output_frame_valid(const gs232b_output_frame_t *frame);

typedef struct {
    gs232b_output_frame_t requested;
    gs232b_output_frame_t last_successful;
    gs232b_time_us_t attempted_at_us;
    gs232b_error_t last_error;
    /* NOTE: 初始状态和提交失败后均为 false。停止请求不能证明输出已停止；
     * true 仅表示已提交，不能表示已从外部测量确认。 */
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
    uint8_t axis_mask; /* NOTE: bit0 为 AZ，bit1 为 EL；非特定轴故障时为零。 */
} gs232b_fault_snapshot_t;

typedef struct {
    gs232b_axis_id_t axis;
    gs232b_axis_mode_t mode;
    gs232b_sample_t sample;
    gs232b_error_t sample_error;
    int32_t position_mdeg; /* NOTE: 机械行程，与 P36/P45/Z 无关。 */
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
    uint16_t current_point; /* NOTE: 从 1 开始；空序列时为 0。 */
    uint16_t point_count;
    gs232b_time_us_t next_deadline_us; /* NOTE: 仅在运行状态下有意义。 */
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

/* NOTE: 这是由所有者在同步边界内复制的值对象；不包含指向驱动、套接字、
 * RTOS 对象或可变共享存储的指针。 */
typedef struct {
    gs232b_time_us_t captured_at_us;
    gs232b_command_epoch_t command_epoch;
    gs232b_session_key_t owner; /* NOTE: 未被占用时无效（所有字段均为零）。 */
    gs232b_axis_snapshot_t axes[GS232B_AXIS_COUNT];
    gs232b_sequence_snapshot_t sequence;
    gs232b_output_snapshot_t output;
    gs232b_fault_snapshot_t fault;
} gs232b_state_snapshot_t;

/* NOTE: 两个轴均禁用；采样、零点和限位均无效；输出未知；epoch 为 1。 */
void gs232b_state_snapshot_init(gs232b_state_snapshot_t *snapshot);

#endif
