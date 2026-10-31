#include "gs232b/speed.h"

#include <stddef.h>

void gs232b_speed_levels_init(gs232b_speed_levels_t *levels)
{
    if (levels != NULL) {
        *levels = (gs232b_speed_levels_t){.permille = {250, 500, 750, 1000}};
    }
}

bool gs232b_speed_levels_valid(const gs232b_speed_levels_t *levels)
{
    if (levels == NULL) {
        return false;
    }
    uint16_t previous = 0;
    for (unsigned i = 0; i < GS232B_SPEED_LEVEL_COUNT; ++i) {
        if (levels->permille[i] <= previous || levels->permille[i] > 1000U) {
            return false;
        }
        previous = levels->permille[i];
    }
    return true;
}

gs232b_error_t gs232b_speed_level_permille(const gs232b_speed_levels_t *levels,
                                          uint8_t level, uint16_t *permille)
{
    if (permille == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *permille = 0;
    if (!gs232b_speed_levels_valid(levels) || level == 0 ||
        level > GS232B_SPEED_LEVEL_COUNT) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *permille = levels->permille[level - 1U];
    return GS232B_OK;
}
