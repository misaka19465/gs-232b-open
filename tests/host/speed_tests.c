#include "gs232b/speed.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); return false; \
} } while (0)

static bool test_defaults_and_custom_levels(void)
{
    gs232b_speed_levels_init(NULL);
    gs232b_speed_levels_t levels;
    gs232b_speed_levels_init(&levels);
    CHECK(gs232b_speed_levels_valid(&levels));
    for (uint8_t level = 1; level <= GS232B_SPEED_LEVEL_COUNT; ++level) {
        uint16_t permille;
        CHECK(gs232b_speed_level_permille(&levels, level, &permille) == GS232B_OK);
        CHECK(permille == level * 250U);
    }
    levels = (gs232b_speed_levels_t){.permille = {175, 410, 635, 970}};
    for (uint8_t level = 1; level <= GS232B_SPEED_LEVEL_COUNT; ++level) {
        uint16_t permille;
        CHECK(gs232b_speed_level_permille(&levels, level, &permille) == GS232B_OK);
        CHECK(permille == levels.permille[level - 1U]);
    }
    levels = (gs232b_speed_levels_t){.permille = {1, 2, 999, 1000}};
    CHECK(gs232b_speed_levels_valid(&levels));
    return true;
}

static bool test_invalid_tables_and_level_numbers(void)
{
    const gs232b_speed_levels_t invalid[] = {
        {.permille = {0, 500, 750, 1000}},
        {.permille = {250, 500, 750, 1001}},
        {.permille = {250, 250, 750, 1000}},
        {.permille = {250, 750, 500, 1000}},
        {.permille = {250, 500, UINT16_MAX, 1000}},
        {.permille = {0, 0, 0, 0}}
    };
    CHECK(!gs232b_speed_levels_valid(NULL));
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(!gs232b_speed_levels_valid(&invalid[i]));
        uint16_t permille = UINT16_MAX;
        CHECK(gs232b_speed_level_permille(&invalid[i], 1, &permille) ==
            GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(permille == 0);
    }
    gs232b_speed_levels_t levels;
    gs232b_speed_levels_init(&levels);
    const uint8_t rejected[] = {0, 5, UINT8_MAX};
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        uint16_t permille = UINT16_MAX;
        CHECK(gs232b_speed_level_permille(&levels, rejected[i], &permille) ==
            GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(permille == 0);
    }
    uint16_t permille = UINT16_MAX;
    CHECK(gs232b_speed_level_permille(NULL, 1, &permille) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(permille == 0);
    CHECK(gs232b_speed_level_permille(&levels, 1, NULL) == GS232B_ERROR_INVALID_ARGUMENT);
    return true;
}

int main(void)
{
    return test_defaults_and_custom_levels() && test_invalid_tables_and_level_numbers() ?
        EXIT_SUCCESS : EXIT_FAILURE;
}
