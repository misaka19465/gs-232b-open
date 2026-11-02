#ifndef GS232B_SPEED_H
#define GS232B_SPEED_H

#include "gs232b/types.h"

#define GS232B_SPEED_LEVEL_COUNT 4U

/* NOTE: 固件调速参数，不是用户校准或持久化格式；四档必须非零且严格递增。
 * 数值是 PWM 请求，不表示实际转速或 SPEED 电压已经确认。 */
typedef struct {
    uint16_t permille[GS232B_SPEED_LEVEL_COUNT];
} gs232b_speed_levels_t;

/** @brief 初始化四档 PWM 请求为 250/500/750/1000‰。 */
void gs232b_speed_levels_init(gs232b_speed_levels_t *levels);

/** @brief 校验四档均在 1..1000‰ 且严格递增；不访问输出。 */
bool gs232b_speed_levels_valid(const gs232b_speed_levels_t *levels);

/**
 * @brief 将 X1..X4 对应的 1..4 档号换算为 PWM 请求。
 * @return 表或档号非法时返回 INVALID_ARGUMENT，并将非空输出置零。
 */
gs232b_error_t gs232b_speed_level_permille(const gs232b_speed_levels_t *levels,
                                          uint8_t level, uint16_t *permille);

#endif
