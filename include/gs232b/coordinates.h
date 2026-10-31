#ifndef GS232B_COORDINATES_H
#define GS232B_COORDINATES_H

#include <stdbool.h>
#include <stdint.h>

#include "gs232b/types.h"

#define GS232B_AZIMUTH_MIN_MDEG INT32_C(0)
#define GS232B_AZIMUTH_MAX_MDEG INT32_C(450000)
#define GS232B_ELEVATION_MIN_MDEG INT32_C(0)
#define GS232B_ELEVATION_MAX_MDEG INT32_C(180000)
/* NOTE: 按 4.5V×3/(4.7kΩ+3kΩ) 四舍五入到整数微伏。 */
#define GS232B_FEEDBACK_FULL_SCALE_UV UINT32_C(1753247)
/* NOTE: 拒绝放大噪声后仍不足 500mV 的有效反馈跨度。 */
#define GS232B_FEEDBACK_MIN_SPAN_UV UINT32_C(500000)

typedef enum {
    GS232B_AZIMUTH_DISPLAY_P36 = 0,
    GS232B_AZIMUTH_DISPLAY_P45
} gs232b_azimuth_display_mode_t;

typedef enum {
    GS232B_AZIMUTH_CENTER_NORTH = 0,
    GS232B_AZIMUTH_CENTER_SOUTH
} gs232b_azimuth_center_t;

typedef struct {
    gs232b_azimuth_display_mode_t mode;
    gs232b_azimuth_center_t center;
} gs232b_azimuth_display_settings_t;

typedef struct {
    bool both_axes_stopped;
    bool sequence_timer_running;
    bool calibration_active;
} gs232b_display_change_context_t;

typedef enum {
    GS232B_ELEVATION_ACTION_MOVE = 0,
    GS232B_ELEVATION_ACTION_ZERO_CALIBRATION,
    GS232B_ELEVATION_ACTION_DISPLAY_CALIBRATION,
    GS232B_ELEVATION_ACTION_STOP
} gs232b_elevation_action_t;

/**
 * @brief 初始化显示设置为 P45 和 N Center。
 * @param settings 输出设置。
 */
void gs232b_azimuth_display_settings_init(gs232b_azimuth_display_settings_t *settings);

/**
 * @brief 生成切换 P36/P45 后的候选设置。
 * @param current 当前设置。
 * @param mode 目标显示模式。
 * @param context 运动、序列和校准状态。
 * @param next 输出候选设置；调用方持久化成功后再提交。
 * @param changed 输出是否发生实际变化。
 * @return 参数有效且允许变更时返回 GS232B_OK。
 * @note 实际变更要求两轴停止、序列计时停止且没有校准交互。重复设置不要求停机。
 *       changed 为 true 时，设置提交成功的同一串行边界须清除已加载或已结束的序列；
 *       持久化失败时须保留旧设置和序列。
 */
gs232b_error_t gs232b_azimuth_display_set_mode(
    const gs232b_azimuth_display_settings_t *current,
    gs232b_azimuth_display_mode_t mode,
    const gs232b_display_change_context_t *context,
    gs232b_azimuth_display_settings_t *next,
    bool *changed);

/**
 * @brief 生成 Z 命令切换 N/S 中心后的候选设置。
 * @param current 当前设置。
 * @param context 运动、序列和校准状态。
 * @param next 输出候选设置；调用方持久化成功后再提交。
 * @param changed 输出是否发生实际变化。
 * @return 参数有效且允许变更时返回 GS232B_OK。
 * @note P45 下 Z 为无操作；P36 下实际切换遵守显示模式变更前置条件。
 */
gs232b_error_t gs232b_azimuth_display_toggle_center(
    const gs232b_azimuth_display_settings_t *current,
    const gs232b_display_change_context_t *context,
    gs232b_azimuth_display_settings_t *next,
    bool *changed);

/**
 * @brief 获取轴固定的机械行程限位。
 * @param axis 轴编号。
 * @param minimum_mdeg 输出最小机械角。
 * @param maximum_mdeg 输出最大机械角。
 * @return 参数有效时返回 GS232B_OK。
 */
gs232b_error_t gs232b_axis_mechanical_limits(gs232b_axis_id_t axis,
                                             int32_t *minimum_mdeg,
                                             int32_t *maximum_mdeg);

/**
 * @brief 将校准 ADC 电压按固定满量程换算为机械行程角。
 * @param axis 轴编号。
 * @param zero_uv 已保存的轴零点电压。
 * @param adc_uv 当前经芯片校准的 ADC 引脚电压。
 * @param position_mdeg 输出机械角，错误时置为零。
 * @return 反馈越界时返回 GS232B_ERROR_HARDWARE_FAULT，校准零点无效时返回 GS232B_ERROR_NOT_READY。
 * @note 该换算只依赖轴、固定满量程和零点，不读取 P36/P45/Z 设置。
 */
gs232b_error_t gs232b_position_from_feedback(gs232b_axis_id_t axis,
                                             uint32_t zero_uv,
                                             uint32_t adc_uv,
                                             int32_t *position_mdeg);

/**
 * @brief 将有效机械方位映射为 GS 显示整数角度。
 * @param settings P36/P45 与 N/S 中心设置。
 * @param position_mdeg 当前机械行程角。
 * @param degrees 输出协议整数角度，错误时置为零。
 * @return 参数有效且机械角在限位内时返回 GS232B_OK。
 * @note 协议角度按半度向上取整；P36 取整后归一到 000..359。
 */
gs232b_error_t gs232b_azimuth_position_to_display(
    const gs232b_azimuth_display_settings_t *settings,
    int32_t position_mdeg,
    uint16_t *degrees);

/**
 * @brief 将协议方位目标逆映射为机械行程目标。
 * @param settings P36/P45 与 N/S 中心设置。
 * @param display_degrees 输入整数协议角度；P36 接受 000..360，P45 接受 000..450。
 * @param reference_position_mdeg 当前机械位置，P36 用于在等价解中选最近目标。
 * @param target_mdeg 输出机械目标，错误时置为零。
 * @return 存在机械限位内的解时返回 GS232B_OK；当前反馈无效时返回 GS232B_ERROR_NOT_READY。
 * @note P36 等距时选择机械行程较小的解；360 与 000 等价。序列后续点可将前一点解析结果作为参考位置。
 */
gs232b_error_t gs232b_azimuth_target_to_position(
    const gs232b_azimuth_display_settings_t *settings,
    uint16_t display_degrees,
    int32_t reference_position_mdeg,
    int32_t *target_mdeg);

/**
 * @brief 将仰角机械位置映射为整数度；未启用时始终返回 000。
 * @param elevation_enabled 仰角轴是否由设备配置启用。
 * @param position_mdeg 当前机械仰角；启用时必须位于固定限位内。
 * @param degrees 输出协议角度，错误时置为零。
 * @return 参数有效且反馈有效时返回 GS232B_OK。
 */
gs232b_error_t gs232b_elevation_position_to_display(bool elevation_enabled,
                                                    int32_t position_mdeg,
                                                    uint16_t *degrees);

/**
 * @brief 校验 W 命令中的仰角目标。
 * @param elevation_enabled 仰角轴是否由设备配置启用。
 * @param target_degrees 输入整数仰角。
 * @return 启用时 000..180 或禁用时仅 000 返回 GS232B_OK。
 */
gs232b_error_t gs232b_elevation_target_validate(bool elevation_enabled,
                                                uint16_t target_degrees);

/**
 * @brief 校验仰角独立命令对当前轴配置是否可用。
 * @param elevation_enabled 仰角轴是否由设备配置启用。
 * @param action U/D、O2、F2 或 E/S 对应的操作。
 * @return 未启用时仅停止操作可用。
 */
gs232b_error_t gs232b_elevation_action_validate(bool elevation_enabled,
                                                gs232b_elevation_action_t action);

#endif
