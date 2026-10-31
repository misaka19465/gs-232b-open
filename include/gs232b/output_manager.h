#ifndef GS232B_OUTPUT_MANAGER_H
#define GS232B_OUTPUT_MANAGER_H

#include "gs232b/ports.h"

/* NOTE: 只允许输出管理器调用此端口。适配器必须在同一总线临界区内发送
 * 完整 shadow byte 并锁存，期间保持 W5500 nSS 为高；不得保留调用参数。
 * pwm_permille 为 0..1000 的方位 PWM 请求，由适配器换算为 LEDC 计数。
 * 返回错误时允许 SPI/PWM 只完成部分更新，调用方不得推断物理输出状态。 */
typedef struct {
    void *ctx;
    gs232b_error_t (*apply)(void *ctx, uint8_t shadow_byte, uint16_t pwm_permille);
} gs232b_output_driver_t;

typedef struct {
    gs232b_output_frame_t requested; /* NOTE: 最新接纳的请求，包含等待中的方向。 */
    gs232b_output_snapshot_t output; /* NOTE: 提交尝试及成功请求镜像，不是电压读回。 */
    uint8_t shadow_byte; /* NOTE: 最近提交尝试的字节；known=false 时不是物理读回值。 */
    bool reversal_waiting[GS232B_AXIS_COUNT];
} gs232b_output_manager_snapshot_t;

/* NOTE: 私有状态布局仅用于静态分配。方向值对应已有轴枚举的 0/1/2；
 * 上次运动方向保留到停机之后，以防 STOP→反向绕过换向等待。 */
typedef struct {
    uint8_t last_direction;
    gs232b_time_us_t stopped_at_us;
    bool stop_confirmed;
    bool history_known;
} gs232b_output_manager_axis_t;

/* NOTE: 每组 595/PWM 外设只有一个管理器和一个串行所有者。所有 API（包括
 * port 回调及快照）必须由同一任务调用或由调用方在同一锁内串行化；回调
 * 不得重入。不得复制活动管理器或直接修改字段。端口 ctx 借用至管理器停用。 */
typedef struct {
    gs232b_clock_t clock;
    gs232b_output_driver_t driver;
    gs232b_time_us_t reversal_wait_us;
    gs232b_output_manager_axis_t axes[GS232B_AXIS_COUNT];
    gs232b_output_manager_snapshot_t state;
} gs232b_output_manager_t;

/**
 * @brief 初始化并提交四路停止、PWM=0、W5500 保持复位（shadow=0x00）。
 * @param manager 尚未活动的管理器。
 * @param clock 单调微秒时钟；端口回调需覆盖硬件提交耗时。
 * @param driver 唯一的底层完整镜像提交端口。
 * @param reversal_wait_us 非零换向停顿，具体值由控制箱实测确定。
 * @return 初始化提交失败时返回驱动错误，输出状态未知，可用 stop_all 重试。
 * @note 上电及故障恢复后方向历史未知，首次运动也需从成功停机完成时等待
 *       完整停顿。不得用本函数重新初始化运行中的管理器以绕过等待。
 */
gs232b_error_t gs232b_output_manager_init(gs232b_output_manager_t *manager,
                                         gs232b_clock_t clock,
                                         gs232b_output_driver_t driver,
                                         gs232b_time_us_t reversal_wait_us);

/**
 * @brief 接纳完整逻辑输出请求，立即提交停机/同向更新，延后反向使能。
 * @param manager 已初始化的管理器。
 * @param frame 使用 AZ STOP/CW/CCW、EL STOP/UP/DOWN 枚举的有效请求。
 * @return OK 表示请求已接纳，等待中的方向须由 tick 推进；未知状态下运动
 *         请求返回 NOT_READY，非法参数不改变请求或访问驱动。
 * @note AZ 停止或等待时实际 PWM 为零，但请求值保留。任何驱动失败都会
 *       撤销两轴待执行动作并清零 PWM 请求；调用方应处理故障并显式全停，
 *       tick 不会自动重试失败的动作。known 仅表示提交成功，不能证明物理停机。
 */
gs232b_error_t gs232b_output_manager_apply(gs232b_output_manager_t *manager,
                                          const gs232b_output_frame_t *frame);

/**
 * @brief 返回面向运动层的完整请求端口；ctx 借用 manager，不得绕过它访问驱动。
 */
gs232b_output_t gs232b_output_manager_port(gs232b_output_manager_t *manager);

/**
 * @brief 更新两轴方向，保留 W5500 bit4 和 PWM 请求；STOP 清除该轴方向位。
 */
gs232b_error_t gs232b_output_manager_set_directions(gs232b_output_manager_t *manager,
                                                   gs232b_az_direction_t azimuth,
                                                   gs232b_el_direction_t elevation);

/**
 * @brief 更新 0..1000 的方位 PWM 请求，保留方向及 W5500 bit4。
 * @note 等待中的方向只由方向请求或 tick 推进；本函数不会因等待到期而启机。
 */
gs232b_error_t gs232b_output_manager_set_pwm(gs232b_output_manager_t *manager,
                                            uint16_t speed_permille);

/**
 * @brief 只更新 W5500 nRESET（asserted=true 时 bit4=0），保留方向及 PWM 请求。
 * @note 等待中的方向不会被此操作使能。输出未知时，reset/PWM 更新只尝试
 *       提交全停镜像。复位脉宽、恢复等待及网络驱动停启由网络层协调。
 */
gs232b_error_t gs232b_output_manager_set_w5500_reset(gs232b_output_manager_t *manager,
                                                    bool asserted);

/**
 * @brief 非阻塞推进两轴独立的换向等待；重复请求或 reset/PWM 更新不重启计时。
 * @return 状态未知时返回 NOT_READY 且不自动重试；提交失败时返回驱动错误。
 */
gs232b_error_t gs232b_output_manager_tick(gs232b_output_manager_t *manager);

/**
 * @brief 撤销两轴待执行动作、清方向和 PWM 请求，保留 W5500 reset 并立即提交。
 * @note 该函数始终尝试输出；失败时 known=false，成功后才能接纳新的运动请求。
 */
gs232b_error_t gs232b_output_manager_stop_all(gs232b_output_manager_t *manager);

/**
 * @brief 在串行所有权边界内复制状态；失败时清零输出快照。
 */
gs232b_error_t gs232b_output_manager_snapshot(
    const gs232b_output_manager_t *manager,
    gs232b_output_manager_snapshot_t *snapshot);

#endif
