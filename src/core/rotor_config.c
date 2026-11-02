#include "gs232b/rotor.h"

// TODO: 按照型号配置其他不同参数
/* NOTE: 1 = G-5500，默认。 */
static void config_1(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
    config->axes[GS232B_AXIS_ELEVATION].enabled = true;
}

/* NOTE: 2 = G-5400B。 */
static void config_2(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
    config->axes[GS232B_AXIS_ELEVATION].enabled = true;
}

/* NOTE: 3 = G-5600B。 */
static void config_3(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
    config->axes[GS232B_AXIS_ELEVATION].enabled = true;
}

/* NOTE: 4 = G-5500DC。 */
static void config_4(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
    config->axes[GS232B_AXIS_ELEVATION].enabled = true;
}

/* NOTE: 5 = G-400。 */
static void config_5(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
}

/* NOTE: 6 = G-500A。 */
static void config_6(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_ELEVATION].enabled = true;
}

/* NOTE: 7 = G-550。 */
static void config_7(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_ELEVATION].enabled = true;
}

/* NOTE: 8 = G-800DXA。 */
static void config_8(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
}

/* NOTE: 9 = G-1000DXA。 */
static void config_9(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
}

/* NOTE: 10 = G-2800DXA。 */
static void config_10(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
}

/* NOTE: 11 = G-800DXC。 */
static void config_11(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
}

/* NOTE: 12 = G-1000DXC。 */
static void config_12(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
}

/* NOTE: 13 = G-2800DXC。 */
static void config_13(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
}

/* NOTE: 14 = SDX 系列。 */
static void config_14(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
}

/* NOTE: 15 = G-400 + G-500A。 */
static void config_15(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
    config->axes[GS232B_AXIS_ELEVATION].enabled = true;
}

/* NOTE: 16 = G-400 + G-550。 */
static void config_16(gs232b_motion_config_t *config)
{
    config->axes[GS232B_AXIS_AZIMUTH].enabled = true;
    config->axes[GS232B_AXIS_ELEVATION].enabled = true;
}

typedef void (*override_fn_t)(gs232b_motion_config_t *config);

/* NOTE: 每个 ID 单独覆盖默认配置，不合并相同轴型，便于后续独立调整参数。 */
static const override_fn_t overrides[] = {
    config_1,
    config_2,
    config_3,
    config_4,
    config_5,
    config_6,
    config_7,
    config_8,
    config_9,
    config_10,
    config_11,
    config_12,
    config_13,
    config_14,
    config_15,
    config_16,
};

_Static_assert(sizeof(overrides) / sizeof(overrides[0]) == GS232B_ROTOR_MODEL_COUNT,
    "rotor ID count must match independent configurations");

bool gs232b_rotor_model_valid(uint32_t model_id)
{
    return model_id >= 1U && model_id <= GS232B_ROTOR_MODEL_COUNT;
}

gs232b_error_t gs232b_rotor_config(uint32_t model_id, gs232b_motion_config_t *config)
{
    if (config == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    gs232b_motion_config_init(config);
    if (!gs232b_rotor_model_valid(model_id)) return GS232B_ERROR_INVALID_ARGUMENT;
    config->model_id = (uint16_t)model_id;
    overrides[model_id - 1U](config);
    return GS232B_OK;
}
