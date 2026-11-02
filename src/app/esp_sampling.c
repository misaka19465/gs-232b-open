#include "esp_app_internal.h"

static void notify(void *ctx)
{
    gs232b_esp_app_t *runtime = ctx;
    /* NOTE: timer task 回调只投递可合并通知，不转换 ADC、不调用控制或 Flash。 */
    (void)xTaskNotifyGive(runtime->sampling_task);
}

static void sampling(void *ctx)
{
    gs232b_esp_app_t *runtime = ctx;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        (void)gs232b_esp_adc_capture(&runtime->adc);
    }
}

gs232b_error_t gs232b_app_adc_open(void *ctx, gs232b_sampler_t *sampler)
{
    gs232b_esp_app_t *runtime = ctx;
    *sampler = gs232b_esp_adc_sampler(&runtime->adc);
    gs232b_error_t error = gs232b_esp_adc_open(&runtime->adc);
    if (error != GS232B_OK) return error;
    if (xTaskCreate(sampling, "gs_adc", 3072, runtime, 9, &runtime->sampling_task) != pdPASS) {
        (void)gs232b_esp_adc_close(&runtime->adc);
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    const esp_timer_create_args_t timer = {.callback = notify, .arg = runtime,
        .dispatch_method = ESP_TIMER_TASK, .name = "gs_adc_tick", .skip_unhandled_events = true};
    if (esp_timer_create(&timer, &runtime->sample_timer) != ESP_OK ||
        esp_timer_start_periodic(runtime->sample_timer, GS232B_APP_SAMPLE_PERIOD_US) != ESP_OK) {
        if (runtime->sample_timer != NULL) (void)esp_timer_delete(runtime->sample_timer);
        runtime->sample_timer = NULL;
        /* NOTE: 尚无通知，任务仍停在入口；只在这个未运行阶段允许直接销毁。 */
        vTaskDelete(runtime->sampling_task);
        runtime->sampling_task = NULL;
        (void)gs232b_esp_adc_close(&runtime->adc);
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    return GS232B_OK;
}
