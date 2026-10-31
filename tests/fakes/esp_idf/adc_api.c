#include "adc_api.h"

#include <assert.h>
#include <string.h>

struct adc_oneshot_unit_ctx_t { int unused; };
struct adc_cali_scheme_t { adc_channel_t channel; };

esp_adc_fake_t esp_adc_fake;
static struct adc_oneshot_unit_ctx_t unit;
static struct adc_cali_scheme_t calibration[2];

void esp_adc_fake_reset(void)
{
    assert(!esp_adc_fake.unit_live);
    assert(!esp_adc_fake.calibration_live[0] && !esp_adc_fake.calibration_live[1]);
    assert(esp_adc_fake.queue_deletes == esp_adc_fake.queue_allocations);
    memset(&esp_adc_fake, 0, sizeof(esp_adc_fake));
    atomic_init(&esp_adc_fake.now_us, 0);
    esp_adc_fake.raw[ADC_CHANNEL_1] = 913;
    esp_adc_fake.raw[ADC_CHANNEL_0] = 1729;
    esp_adc_fake.voltage_mv[ADC_CHANNEL_1] = 600;
    esp_adc_fake.voltage_mv[ADC_CHANNEL_0] = 1100;
}

int64_t esp_timer_get_time(void)
{
    return atomic_load(&esp_adc_fake.now_us);
}

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *config,
                               adc_oneshot_unit_handle_t *ret_unit)
{
    ++esp_adc_fake.unit_creates;
    esp_adc_fake.unit_config = *config;
    if (esp_adc_fake.unit_create_error != ESP_OK) {
        return esp_adc_fake.unit_create_error;
    }
    assert(!esp_adc_fake.unit_live);
    esp_adc_fake.unit_live = true;
    *ret_unit = &unit;
    return ESP_OK;
}

esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle,
                                     adc_channel_t channel,
                                     const adc_oneshot_chan_cfg_t *config)
{
    assert(handle == &unit && esp_adc_fake.unit_live);
    assert(channel == ADC_CHANNEL_0 || channel == ADC_CHANNEL_1);
    esp_adc_fake.channel_config[channel] = *config;
    return esp_adc_fake.config_error[channel];
}

esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle,
                           adc_channel_t channel, int *out_raw)
{
    assert(handle == &unit && esp_adc_fake.unit_live);
    assert(channel == ADC_CHANNEL_0 || channel == ADC_CHANNEL_1);
    ++esp_adc_fake.reads[channel];
    esp_adc_fake.now_us += esp_adc_fake.read_duration_us[channel];
    *out_raw = esp_adc_fake.raw[channel];
    return esp_adc_fake.read_error[channel];
}

esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t handle)
{
    assert(handle == &unit && esp_adc_fake.unit_live);
    ++esp_adc_fake.unit_deletes;
    if (esp_adc_fake.unit_delete_error != ESP_OK) {
        return esp_adc_fake.unit_delete_error;
    }
    assert(!esp_adc_fake.calibration_live[0] && !esp_adc_fake.calibration_live[1]);
    esp_adc_fake.unit_live = false;
    return ESP_OK;
}

esp_err_t adc_cali_create_scheme_curve_fitting(
    const adc_cali_curve_fitting_config_t *config, adc_cali_handle_t *ret_handle)
{
    assert(esp_adc_fake.unit_live);
    const adc_channel_t channel = config->chan;
    assert(channel == ADC_CHANNEL_0 || channel == ADC_CHANNEL_1);
    ++esp_adc_fake.calibration_creates[channel];
    esp_adc_fake.calibration_config[channel] = *config;
    if (esp_adc_fake.calibration_create_error[channel] != ESP_OK) {
        return esp_adc_fake.calibration_create_error[channel];
    }
    assert(!esp_adc_fake.calibration_live[channel]);
    esp_adc_fake.calibration_live[channel] = true;
    calibration[channel].channel = channel;
    *ret_handle = &calibration[channel];
    return ESP_OK;
}

esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t handle, int raw, int *voltage)
{
    assert(handle == &calibration[0] || handle == &calibration[1]);
    const adc_channel_t channel = handle->channel;
    assert(esp_adc_fake.calibration_live[channel]);
    ++esp_adc_fake.conversions[channel];
    esp_adc_fake.last_converted_raw[channel] = raw;
    esp_adc_fake.now_us += esp_adc_fake.conversion_duration_us[channel];
    *voltage = esp_adc_fake.voltage_mv[channel];
    return esp_adc_fake.convert_error[channel];
}

esp_err_t adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t handle)
{
    assert(handle == &calibration[0] || handle == &calibration[1]);
    const adc_channel_t channel = handle->channel;
    assert(esp_adc_fake.calibration_live[channel]);
    ++esp_adc_fake.calibration_deletes[channel];
    if (esp_adc_fake.calibration_delete_error[channel] != ESP_OK) {
        return esp_adc_fake.calibration_delete_error[channel];
    }
    esp_adc_fake.calibration_live[channel] = false;
    return ESP_OK;
}
