#ifndef GS232B_SEQUENCE_STORE_H
#define GS232B_SEQUENCE_STORE_H

#include "gs232b/coordinates.h"
#include "gs232b/position_parser.h"

/** @brief 执行端提供的只读坐标/能力快照，验证期间不得改变。 */
typedef struct {
    gs232b_azimuth_display_settings_t display;
    gs232b_axis_snapshot_t axes[GS232B_AXIS_COUNT];
    bool elevation_enabled;
} gs232b_position_context_t;

/** @brief 完整参数通过验证后才能执行的首点及旧序列遗留轴停机计划。 */
typedef struct {
    int32_t targets_mdeg[GS232B_AXIS_COUNT];
    uint8_t axis_mask;
    uint8_t stop_mask;
} gs232b_position_plan_t;

/** @brief 已验证的机械角序列；angles 为整数度，W 按 AZ/EL 交错。 */
typedef struct {
    char command;
    uint16_t interval_s;
    uint16_t point_count;
    const uint16_t *angles;
} gs232b_sequence_data_t;

/* NOTE: 私有布局仅供静态分配；两份角度各 7600 字节，无整行副本。
 * staging 为协议角度；完整验证后编译为机械角并交换，active 无需重解释坐标。 */
typedef struct {
    uint16_t banks[2][GS232B_POSITION_MAX_ANGLES];
    unsigned active_bank;
    gs232b_sequence_data_t active;
    gs232b_position_plan_t first;
} gs232b_sequence_store_t;

/** @brief 初始化尚未活动的双缓冲序列存储。 */
void gs232b_sequence_store_init(gs232b_sequence_store_t *store);
/** @brief 返回唯一 staging；调用方必须先获得输入预约，不能与 active 共用。 */
uint16_t *gs232b_sequence_store_staging(gs232b_sequence_store_t *store);
/** @brief 清除有效序列元数据；不改变轴动作，不擦写角度数组。 */
void gs232b_sequence_store_clear(gs232b_sequence_store_t *store);
/**
 * @brief 纯校验全部角度、间隔、轴就绪及缩小后的机械限位，生成首点计划。
 * @note P36 首点参考当前位置，后续点逐点连续选解；不修改任何序列或动作。
 */
gs232b_error_t gs232b_sequence_store_validate(
    const gs232b_sequence_store_t *store, const gs232b_position_command_t *command,
    const gs232b_position_context_t *context, gs232b_position_plan_t *plan);
/**
 * @brief 首点执行成功后，原地编译并交换 active/staging；单点清旧序列。
 * @note command 必须指向本对象的 staging，且刚用同一 context 完整验证通过。
 *       此函数无失败路径；调用后旧 active 视图失效，新 active 保留到下次提交/清除。
 */
void gs232b_sequence_store_commit(gs232b_sequence_store_t *store,
                                   const gs232b_position_command_t *command,
                                   const gs232b_position_context_t *context,
                                   const gs232b_position_plan_t *plan);

#endif
