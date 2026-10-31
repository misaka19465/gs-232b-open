#ifndef TEST_LEDC_API_H
#define TEST_LEDC_API_H

#include "driver/ledc.h"

typedef enum {
    LEDC_FAKE_TIMER_OPEN,
    LEDC_FAKE_CHANNEL_OPEN,
    LEDC_FAKE_SET,
    LEDC_FAKE_UPDATE,
    LEDC_FAKE_STOP,
    LEDC_FAKE_CHANNEL_CLOSE,
    LEDC_FAKE_PAUSE,
    LEDC_FAKE_TIMER_CLOSE,
    LEDC_FAKE_OP_COUNT
} ledc_fake_op_t;

typedef struct {
    ledc_timer_config_t timer;
    ledc_channel_config_t channel;
    unsigned calls[LEDC_FAKE_OP_COUNT];
    esp_err_t fail_next[LEDC_FAKE_OP_COUNT];
    uint32_t pending_duty;
    uint32_t submitted_duty;
    bool timer_open;
    bool channel_open;
    bool paused;
    bool invalid_call;
} esp_ledc_fake_t;

extern esp_ledc_fake_t esp_ledc_fake;
void esp_ledc_fake_reset(void);

#endif
