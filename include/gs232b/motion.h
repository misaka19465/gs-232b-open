#ifndef GS232B_MOTION_H
#define GS232B_MOTION_H

#include "gs232b/feedback.h"
#include "gs232b/output_manager.h"

#define GS232B_MOTION_AZ_MASK UINT8_C(0x01)
#define GS232B_MOTION_EL_MASK UINT8_C(0x02)
#define GS232B_MOTION_ALL_AXES UINT8_C(0x03)

typedef enum {
    GS232B_MOTION_NEGATIVE = -1, /* NOTE: AZ CCW / EL DOWN。 */
    GS232B_MOTION_STOP = 0,
    GS232B_MOTION_POSITIVE = 1   /* NOTE: AZ CW / EL UP。 */
} gs232b_motion_direction_t;

/* NOTE: 固件配置，不是用户校准格式；限位只能缩小固定机械行程。
 * zero_valid=false 或 enabled=false 时轴为 DISABLED。 */
typedef struct {
    bool enabled;
    bool zero_valid;
    uint32_t zero_uv;
    int32_t minimum_mdeg;
    int32_t maximum_mdeg;
    int32_t tolerance_mdeg;
    int32_t slowdown_mdeg;
    int32_t progress_mdeg;
    int32_t reverse_mdeg;
    gs232b_time_us_t no_progress_timeout_us;
    gs232b_time_us_t run_timeout_us;
} gs232b_motion_axis_config_t;

typedef struct {
    gs232b_motion_axis_config_t axes[GS232B_AXIS_COUNT];
    uint16_t azimuth_speed_permille;
    uint16_t azimuth_slow_speed_permille;
} gs232b_motion_config_t;

typedef struct {
    gs232b_time_us_t captured_at_us;
    gs232b_axis_snapshot_t axes[GS232B_AXIS_COUNT];
    gs232b_output_snapshot_t output;
    gs232b_fault_snapshot_t fault;
} gs232b_motion_snapshot_t;

/* NOTE: 私有状态布局仅用于静态分配，不得由调用方修改。计时从实际方向
 * 成功提交后开始；同向替换命令不重置连续运行或无进展计时。 */
typedef struct {
    gs232b_feedback_filter_t filter;
    gs232b_axis_mode_t intent;
    gs232b_motion_direction_t manual_direction;
    int32_t raw_position_mdeg;
    bool raw_position_valid;
    bool sample_seen;
    gs232b_time_us_t latest_sample_at_us;
    int8_t watched_direction;
    gs232b_time_us_t run_started_at_us;
    gs232b_time_us_t progress_at_us;
    int64_t progress_position_mdeg;
    int64_t furthest_position_mdeg;
} gs232b_motion_axis_t;

/* NOTE: 与输出管理器共用一个串行所有者；回调不得重入。manager 和采样
 * ctx 的生命周期必须覆盖本对象；不得复制活动对象。外部只能串行更新
 * manager 的 W5500 reset，其提交失败后须立即调用 motion_tick 锁定故障，
 * 再做任何恢复提交；方向、PWM、等待推进和停机由本模块独占。
 * 采样和 tick 必须周期调用；任何阻塞或停止调度都超出软件停机保证。 */
typedef struct {
    gs232b_output_manager_t *manager;
    gs232b_sampler_t sampler;
    gs232b_motion_config_t config;
    gs232b_motion_axis_t axes[GS232B_AXIS_COUNT];
    gs232b_motion_snapshot_t state;
    bool initialized;
} gs232b_motion_t;

/**
 * @brief 初始化固件配置，两轴默认禁用且没有零点。
 * @note 初始参数为容差 0.5°、AZ 减速区 15°、进展阈值 0.1°、反向阈值
 *       1°、无进展 5s、连续运行 300s、AZ PWM 800/300‰。
 *       这些是开发起点，部署前须按控制箱实测调整；EL 不支持 PWM 减速。
 */
void gs232b_motion_config_init(gs232b_motion_config_t *config);

/**
 * @brief 借用已初始化的输出管理器并强制全停，建立非阻塞双轴控制器。
 * @param motion 尚未活动的对象，不得借此重新初始化运行中的控制器。
 * @param manager 提供唯一输出和单调时钟的输出管理器。
 * @param sampler 提供 ADC 校准电压和真实采样时间的非阻塞端口。
 * @param config 固件参数及已加载的零点；按值复制。
 * @return 参数无效或全停失败时返回对应错误；缺零点/采样不阻止初始化。
 */
gs232b_error_t gs232b_motion_init(gs232b_motion_t *motion,
                                 gs232b_output_manager_t *manager,
                                 gs232b_sampler_t sampler,
                                 const gs232b_motion_config_t *config);

/**
 * @brief 在两轴均无动作且输出已知全停时更新一个轴的固件配置/零点。
 * @note 可在 FAULT 中修复配置；更新不会解除锁定，位置须由后续 tick 刷新。
 */
gs232b_error_t gs232b_motion_configure_axis(gs232b_motion_t *motion,
                                           gs232b_axis_id_t axis,
                                           const gs232b_motion_axis_config_t *config);

/**
 * @brief 在已知全停的串行边界重新采样，持久化成功后原子应用指定轴零点。
 * @note 不复用进入校准前的滤波窗口；采样、跨度或存储失败均保留原配置。
 *       store.save 必须遵守失败保留旧记录的存储契约，不得重入运动控制。
 */
gs232b_error_t gs232b_motion_save_zero(gs232b_motion_t *motion,
                                      const gs232b_store_t *store,
                                      gs232b_axis_id_t axis);

/**
 * @brief 手动控制指定轴，另一轴保持当前动作；STOP 不要求校准或反馈有效。
 * @return 缺零点、轴禁用或反馈无效时返回 NOT_READY；FAULT 中拒绝新运动。
 * @note 启动前推进一次安全检查；在限位处向外运动会锁定 LIMIT 故障。
 */
gs232b_error_t gs232b_motion_manual(gs232b_motion_t *motion,
                                   gs232b_axis_id_t axis,
                                   gs232b_motion_direction_t direction);

/**
 * @brief 原子接纳 axis_mask 中的机械角目标，未选轴保持动作。
 * @param targets_mdeg 两轴目标数组；只读取 mask 中的目标，单位为毫度。
 * @note 所有选中轴均须就绪且目标可达，否则不替换任何目标。
 *       正常到位清除动作和目标，不会因后续反馈漂移自动重新运动。
 */
gs232b_error_t gs232b_motion_goto(gs232b_motion_t *motion, uint8_t axis_mask,
                                 const int32_t targets_mdeg[GS232B_AXIS_COUNT]);

/**
 * @brief 更新手动/定位的 AZ 速度（1..1000‰），近目标时使用两档中的较小值。
 * @note 立即生效，不重置运行计时；FAULT 中拒绝更新。
 */
gs232b_error_t gs232b_motion_set_speed(gs232b_motion_t *motion,
                                      uint16_t speed_permille);

/**
 * @brief 撤销所选轴的动作和目标并立即清方向；全选时强制提交全停。
 * @note 停止始终可用；FAULT 不会被解除。输出失败会升级为全停故障处理。
 */
gs232b_error_t gs232b_motion_stop(gs232b_motion_t *motion, uint8_t axis_mask);

/**
 * @brief 采样、执行安全检查并推进到位、减速及两轴独立的换向等待。
 * @return FAULT 锁定期间返回所记录的 fault.error；不自动重试失败输出。
 * @note 原始电压/位置先检查异常和越限，滤波位置用于定位和进展检测。
 *       任一受监测轴故障会撤销两轴动作并尝试一次强制全停。
 */
gs232b_error_t gs232b_motion_tick(gs232b_motion_t *motion);

/**
 * @brief 外部调度器锁定故障，撤销两轴动作并尝试一次全停，保留首次诊断。
 * @param code 有效的非 NONE 故障码；非特定轴故障记录 axis_mask=0。
 * @param error 非 OK 的故障原因，也作为返回值；不自动重试停机或恢复动作。
 * @note 与 tick 共用故障路径；停机失败保留输出未知，只能显式停止或清故障。
 */
gs232b_error_t gs232b_motion_abort(gs232b_motion_t *motion,
                                  gs232b_fault_code_t code, gs232b_error_t error);

/**
 * @brief 显式全停并重新验证启用且已校准轴的反馈和限位后解除 FAULT。
 * @note 全停提交失败或反馈仍异常时保留锁定；成功也不会恢复旧动作/目标。
 *       反接、卡滞等静止时不可观测的原因须由操作者修复后再清除。
 */
gs232b_error_t gs232b_motion_clear_fault(gs232b_motion_t *motion);

/**
 * @brief 在串行边界内复制值快照，不采样也不推进运动；错误时清零快照。
 */
gs232b_error_t gs232b_motion_snapshot(const gs232b_motion_t *motion,
                                     gs232b_motion_snapshot_t *snapshot);

#endif
