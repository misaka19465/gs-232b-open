#ifndef TEST_NVS_API_H
#define TEST_NVS_API_H

#include "nvs.h"
#include "gs232b/ports.h"

typedef enum {
    FAKE_NVS_SET_BLOB = 0,
    FAKE_NVS_COMMIT_DATA,
    FAKE_NVS_SET_SELECTOR,
    FAKE_NVS_COMMIT_SELECTOR,
    FAKE_NVS_ERASE,
    FAKE_NVS_COMMIT_ERASE,
    FAKE_NVS_STAGE_COUNT
} fake_nvs_stage_t;

void fake_nvs_reset(void);
void fake_nvs_fail(fake_nvs_stage_t stage, gs232b_error_t error);

#endif
