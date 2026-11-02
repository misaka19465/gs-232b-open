#include "gs232b/app.h"

void gs232b_app_board_config(gs232b_motion_config_t *config, gs232b_time_us_t *reversal_us)
{
    if (config == NULL || reversal_us == NULL) return;
    gs232b_motion_config_init(config);
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
    config->axes[GS232B_AXIS_ELEVATION].enabled = true;
    *reversal_us = UINT64_C(250000);
}
