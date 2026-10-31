#include "ledc_api.h"

#include <string.h>

esp_ledc_fake_t esp_ledc_fake;

void esp_ledc_fake_reset(void)
{
    memset(&esp_ledc_fake, 0, sizeof(esp_ledc_fake));
}

static esp_err_t operation(ledc_fake_op_t op, bool valid)
{
    ++esp_ledc_fake.calls[op];
    if (!valid) {
        esp_ledc_fake.invalid_call = true;
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t error = esp_ledc_fake.fail_next[op];
    esp_ledc_fake.fail_next[op] = ESP_OK;
    return error;
}

esp_err_t ledc_timer_config(const ledc_timer_config_t *config)
{
    const ledc_fake_op_t op = config->deconfigure ? LEDC_FAKE_TIMER_CLOSE :
                                                   LEDC_FAKE_TIMER_OPEN;
    const bool valid = config->speed_mode == LEDC_LOW_SPEED_MODE &&
        config->timer_num == LEDC_TIMER_0 &&
        (config->deconfigure ? esp_ledc_fake.timer_open && esp_ledc_fake.paused &&
                              !esp_ledc_fake.channel_open :
                              !esp_ledc_fake.timer_open &&
                              config->duty_resolution == LEDC_TIMER_10_BIT &&
                              config->freq_hz == 20000 && config->clk_cfg == LEDC_AUTO_CLK);
    const esp_err_t error = operation(op, valid);
    if (error == ESP_OK) {
        esp_ledc_fake.timer_open = !config->deconfigure;
        if (!config->deconfigure) {
            esp_ledc_fake.timer = *config;
            esp_ledc_fake.paused = false;
        }
    }
    return error;
}

esp_err_t ledc_channel_config(const ledc_channel_config_t *config)
{
    const ledc_fake_op_t op = config->deconfigure ? LEDC_FAKE_CHANNEL_CLOSE :
                                                   LEDC_FAKE_CHANNEL_OPEN;
    const bool valid = config->speed_mode == LEDC_LOW_SPEED_MODE &&
        config->channel == LEDC_CHANNEL_0 && esp_ledc_fake.timer_open &&
        (config->deconfigure || (config->gpio_num == 20 &&
         config->timer_sel == LEDC_TIMER_0 && config->duty == 0 && config->hpoint == 0 &&
         !config->flags.output_invert));
    const esp_err_t error = operation(op, valid);
    if (!config->deconfigure && valid) {
        esp_ledc_fake.channel = *config;
        esp_ledc_fake.pending_duty = config->duty;
        esp_ledc_fake.submitted_duty = config->duty;
        esp_ledc_fake.channel_open = true;
    } else if (error == ESP_OK) {
        esp_ledc_fake.channel_open = false;
    }
    return error;
}

esp_err_t ledc_set_duty(ledc_mode_t mode, ledc_channel_t channel, uint32_t duty)
{
    const esp_err_t error = operation(LEDC_FAKE_SET,
        mode == LEDC_LOW_SPEED_MODE && channel == LEDC_CHANNEL_0 &&
        esp_ledc_fake.channel_open && duty <= 1024U);
    if (error == ESP_OK) { esp_ledc_fake.pending_duty = duty; }
    return error;
}

esp_err_t ledc_update_duty(ledc_mode_t mode, ledc_channel_t channel)
{
    const esp_err_t error = operation(LEDC_FAKE_UPDATE,
        mode == LEDC_LOW_SPEED_MODE && channel == LEDC_CHANNEL_0 &&
        esp_ledc_fake.channel_open);
    /* NOTE: 更新失败也模拟部分生效，防止调用方根据旧值宣称物理状态已知。 */
    esp_ledc_fake.submitted_duty = esp_ledc_fake.pending_duty;
    return error;
}

esp_err_t ledc_stop(ledc_mode_t mode, ledc_channel_t channel, uint32_t idle_level)
{
    const esp_err_t error = operation(LEDC_FAKE_STOP,
        mode == LEDC_LOW_SPEED_MODE && channel == LEDC_CHANNEL_0 &&
        esp_ledc_fake.timer_open && idle_level == 0);
    if (error == ESP_OK) { esp_ledc_fake.submitted_duty = 0; }
    return error;
}

esp_err_t ledc_timer_pause(ledc_mode_t mode, ledc_timer_t timer)
{
    const esp_err_t error = operation(LEDC_FAKE_PAUSE,
        mode == LEDC_LOW_SPEED_MODE && timer == LEDC_TIMER_0 &&
        esp_ledc_fake.timer_open && !esp_ledc_fake.channel_open);
    if (error == ESP_OK) { esp_ledc_fake.paused = true; }
    return error;
}
