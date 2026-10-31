#ifndef TEST_ESP_ADC_API_H
#define TEST_ESP_ADC_API_H

#include <stdbool.h>
#include <stdatomic.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"

/* NOTE: 固定为项目使用的 IDF API 子集；数组按 ADC 通道编号索引。
 * 这些替身仅用于原生测试，不加入固件 include 路径或产品代码。 */
typedef struct {
    atomic_int_fast64_t now_us;
    esp_err_t unit_create_error;
    esp_err_t unit_delete_error;
    esp_err_t config_error[2];
    esp_err_t calibration_create_error[2];
    esp_err_t calibration_delete_error[2];
    esp_err_t read_error[2];
    esp_err_t convert_error[2];
    int raw[2];
    int voltage_mv[2];
    int last_converted_raw[2];
    int64_t read_duration_us[2];
    int64_t conversion_duration_us[2];
    unsigned reads[2];
    unsigned conversions[2];
    unsigned unit_creates;
    unsigned unit_deletes;
    unsigned calibration_creates[2];
    unsigned calibration_deletes[2];
    unsigned queue_creates;
    unsigned queue_allocations;
    unsigned queue_deletes;
    unsigned fail_queue_create_at;
    unsigned queue_peeks;
    unsigned queue_overwrites;
    bool unit_live;
    bool calibration_live[2];
    adc_oneshot_unit_init_cfg_t unit_config;
    adc_oneshot_chan_cfg_t channel_config[2];
    adc_cali_curve_fitting_config_t calibration_config[2];
} esp_adc_fake_t;

extern esp_adc_fake_t esp_adc_fake;
void esp_adc_fake_reset(void);

#endif
