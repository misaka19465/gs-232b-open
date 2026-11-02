#include "gs232b/app.h"

void gs232b_app_board_config(gs232b_motion_config_t *config, gs232b_time_us_t *reversal_us)
{
    if (config == NULL || reversal_us == NULL) return;
    gs232b_motion_config_init(config);
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
    config->axes[GS232B_AXIS_ELEVATION].enabled = true;
    /* NOTE: 换向 250ms、四档 PWM、容差和超时均为开发起点；完成控制箱实测、
     * 核对最坏调度延迟及模拟 SPEED 响应后，方可更新参数并打开此固件门禁。 */
    config->parameters_verified = false;
    *reversal_us = UINT64_C(250000);
}
