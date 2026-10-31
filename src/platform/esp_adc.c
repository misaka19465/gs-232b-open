#include "gs232b/esp_adc.h"
#include "gs232b/feedback.h"

#include <string.h>

#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"

/* NOTE: C3 的最高衰减档覆盖板上约 1.753V 满量程；有效测量范围上限
 * 约 2.5V。超过板上反馈满量程但仍在 ADC 量程内的电压交给核心报故障。 */
#define BOARD_ADC_ATTEN ADC_ATTEN_DB_12
#define BOARD_ADC_BITWIDTH ADC_BITWIDTH_12
#define BOARD_ADC_MAX_MV 2500
#define BOARD_ADC_MAX_RAW 4095

static const adc_channel_t channels[GS232B_AXIS_COUNT] = {
    [GS232B_AXIS_AZIMUTH] = ADC_CHANNEL_1,
    [GS232B_AXIS_ELEVATION] = ADC_CHANNEL_0
};

static gs232b_error_t map_error(esp_err_t error)
{
    switch (error) {
    case ESP_OK: return GS232B_OK;
    case ESP_ERR_INVALID_ARG: return GS232B_ERROR_INVALID_ARGUMENT;
    case ESP_ERR_NO_MEM: return GS232B_ERROR_RESOURCE_EXHAUSTED;
    case ESP_ERR_NOT_FOUND: return GS232B_ERROR_BUSY;
    case ESP_ERR_INVALID_STATE: return GS232B_ERROR_NOT_READY;
    case ESP_ERR_TIMEOUT: return GS232B_ERROR_TIMEOUT;
    default: return GS232B_ERROR_IO;
    }
}

static gs232b_error_t read_latest(void *ctx, gs232b_axis_id_t axis,
                                  gs232b_sample_t *sample)
{
    if (sample == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    memset(sample, 0, sizeof(*sample));
    gs232b_esp_adc_t *adapter = ctx;
    if (adapter == NULL || axis < GS232B_AXIS_AZIMUTH || axis >= GS232B_AXIS_COUNT) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!adapter->opened) { return GS232B_ERROR_NOT_READY; }

    gs232b_sample_t next = {0};
    /* NOTE: 单槽队列只复制完整样本；读取不等待生产者，也不刷新获取时间。 */
    if (xQueuePeek(adapter->latest[axis], &next, 0) != pdPASS || !next.valid) {
        return GS232B_ERROR_NOT_READY;
    }
    const int64_t now_us = esp_timer_get_time();
    if (now_us < 0 || next.acquired_at_us > (uint64_t)now_us ||
        (uint64_t)now_us - next.acquired_at_us > GS232B_FEEDBACK_SAMPLE_MAX_AGE_US) {
        return GS232B_ERROR_NOT_READY;
    }
    *sample = next;
    return GS232B_OK;
}

gs232b_sampler_t gs232b_esp_adc_sampler(gs232b_esp_adc_t *adapter)
{
    return (gs232b_sampler_t){.ctx = adapter, .read = read_latest};
}

gs232b_error_t gs232b_esp_adc_capture(gs232b_esp_adc_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened) { return GS232B_ERROR_NOT_READY; }

    gs232b_error_t result = GS232B_OK;
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        gs232b_sample_t sample = {0};
        int raw = -1;
        const esp_err_t read_error = adc_oneshot_read(adapter->unit, channels[axis], &raw);
        /* NOTE: 在原始采样完成时记时；校准耗时和缓存读取不能延长样本寿命。 */
        const int64_t acquired_at_us = esp_timer_get_time();
        int voltage_mv = -1;
        if (read_error == ESP_OK && raw >= 0 && raw <= BOARD_ADC_MAX_RAW &&
            acquired_at_us >= 0 &&
            adc_cali_raw_to_voltage(adapter->calibration[axis], raw, &voltage_mv) == ESP_OK &&
            voltage_mv >= 0 && voltage_mv <= BOARD_ADC_MAX_MV) {
            sample = (gs232b_sample_t){
                .acquired_at_us = (uint64_t)acquired_at_us,
                .raw = (uint16_t)raw,
                .adc_uv = (uint32_t)voltage_mv * 1000U,
                .valid = true
            };
        } else {
            result = GS232B_ERROR_NOT_READY;
        }
        /* NOTE: IDF 保证长度为 1 的队列 overwrite 零等待且始终成功；即使
         * 本次采集失败也覆盖旧值，不能继续向运动核心提供上次的有效电压。 */
        (void)xQueueOverwrite(adapter->latest[axis], &sample);
    }
    return result;
}

gs232b_error_t gs232b_esp_adc_close(gs232b_esp_adc_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    adapter->opened = false;
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        if (adapter->latest[axis] != NULL) {
            vQueueDelete(adapter->latest[axis]);
            adapter->latest[axis] = NULL;
        }
        if (adapter->calibration[axis] != NULL) {
            const esp_err_t error = adc_cali_delete_scheme_curve_fitting(
                adapter->calibration[axis]);
            if (error != ESP_OK) { return map_error(error); }
            adapter->calibration[axis] = NULL;
        }
    }
    if (adapter->unit != NULL) {
        const esp_err_t error = adc_oneshot_del_unit(adapter->unit);
        if (error != ESP_OK) { return map_error(error); }
        adapter->unit = NULL;
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_esp_adc_open(gs232b_esp_adc_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (adapter->opened || adapter->unit != NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    for (int axis = 0; axis < GS232B_AXIS_COUNT; ++axis) {
        if (adapter->calibration[axis] != NULL || adapter->latest[axis] != NULL) {
            return GS232B_ERROR_INVALID_ARGUMENT;
        }
    }

    const adc_oneshot_unit_init_cfg_t unit = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };
    esp_err_t error = adc_oneshot_new_unit(&unit, &adapter->unit);
    gs232b_error_t result = map_error(error);
    const adc_oneshot_chan_cfg_t channel = {
        .atten = BOARD_ADC_ATTEN,
        .bitwidth = BOARD_ADC_BITWIDTH
    };
    for (int axis = 0; result == GS232B_OK && axis < GS232B_AXIS_COUNT; ++axis) {
        error = adc_oneshot_config_channel(adapter->unit, channels[axis], &channel);
        result = map_error(error);
        if (result != GS232B_OK) { break; }
        const adc_cali_curve_fitting_config_t calibration = {
            .unit_id = ADC_UNIT_1,
            .chan = channels[axis],
            .atten = BOARD_ADC_ATTEN,
            .bitwidth = BOARD_ADC_BITWIDTH
        };
        /* NOTE: C3 由曲线驱动校验出厂 eFuse；任何失败均保持未就绪，
         * 不使用默认 Vref，也不假定 3.3V 对应原始计数满量程。 */
        error = adc_cali_create_scheme_curve_fitting(&calibration,
                                                      &adapter->calibration[axis]);
        if (error != ESP_OK) {
            result = GS232B_ERROR_NOT_READY;
            break;
        }
        adapter->latest[axis] = xQueueCreate(1, sizeof(gs232b_sample_t));
        if (adapter->latest[axis] == NULL) {
            result = GS232B_ERROR_RESOURCE_EXHAUSTED;
        }
    }
    if (result != GS232B_OK) {
        (void)gs232b_esp_adc_close(adapter);
        return result;
    }
    adapter->opened = true;
    return GS232B_OK;
}
