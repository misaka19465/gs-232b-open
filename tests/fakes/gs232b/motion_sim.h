#ifndef GS232B_MOTION_SIM_H
#define GS232B_MOTION_SIM_H

#include "gs232b/motion.h"

/* NOTE: 明确标注的仿真运动模型，仅用于主机行为验收，不能代表实测控制箱。
 * 理想一阶积分器，无惯性/负载/背隙：AZ 10°/s×PWM，EL 固定 6°/s；
 * 通过真实 output_manager 的 shadow/PWM 驱动，再反算 ADC 微伏。
 * 每次 step 先按上次成功输出积分，再推进虚拟时间并调用真实 motion tick。
 * 支持卡滞、反接、无效/冻结时间戳/越界 ADC、采样/输出错误和提交耗时。
 * 不使用墙上时钟、休眠、实际 GPIO 或真实电机。 */
typedef struct {
    gs232b_motion_t motion;
    gs232b_output_manager_t manager;
    gs232b_time_us_t now_us;
    gs232b_time_us_t driver_latency_us;
    int64_t position_udeg[GS232B_AXIS_COUNT];
    uint32_t zero_uv[GS232B_AXIS_COUNT];
    bool stalled[GS232B_AXIS_COUNT];
    bool reversed[GS232B_AXIS_COUNT];
    bool sample_invalid[GS232B_AXIS_COUNT];
    bool sample_frozen[GS232B_AXIS_COUNT];
    gs232b_time_us_t sample_at_us[GS232B_AXIS_COUNT];
    gs232b_error_t sample_error[GS232B_AXIS_COUNT];
    bool adc_override[GS232B_AXIS_COUNT];
    uint32_t adc_uv[GS232B_AXIS_COUNT];
    gs232b_error_t output_error;
    size_t output_failures;
    uint8_t shadow;
    uint16_t pwm_permille;
    size_t output_attempts;
    bool unsafe_output;
    int8_t last_direction[GS232B_AXIS_COUNT];
    bool stop_known[GS232B_AXIS_COUNT];
    gs232b_time_us_t stopped_at_us[GS232B_AXIS_COUNT];
    gs232b_time_us_t reversal_wait_us;
} gs232b_motion_sim_t;

void gs232b_motion_sim_config(gs232b_motion_config_t *config);
gs232b_error_t gs232b_motion_sim_init(gs232b_motion_sim_t *sim,
                                      const gs232b_motion_config_t *config,
                                      gs232b_time_us_t start_us,
                                      gs232b_time_us_t reversal_wait_us);
gs232b_error_t gs232b_motion_sim_step(gs232b_motion_sim_t *sim,
                                      gs232b_time_us_t delta_us);
int32_t gs232b_motion_sim_position(const gs232b_motion_sim_t *sim, gs232b_axis_id_t axis);

#endif
