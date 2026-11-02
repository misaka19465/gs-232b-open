#include "app/esp_app_internal.h"

static gs232b_esp_app_t runtime;

static void control(void *ctx)
{
    gs232b_esp_app_t *app = ctx;
    TickType_t deadline = xTaskGetTickCount();
    while (app->app.status.phase != GS232B_APP_FAILED) {
        (void)gs232b_app_step(&app->app);
        if (app->app.status.phase == GS232B_APP_UART_BOOT) {
            /* NOTE: 匹配窗口内尚未开放运动，1 tick 轮询避免积压单槽 RMT 完成事件。 */
            vTaskDelay(1);
            deadline = xTaskGetTickCount();
            continue;
        }
        /* NOTE: 绝对周期避免执行耗时累计漂移；过期不补跑历史运动/采样。 */
        if (xTaskDelayUntil(&deadline, pdMS_TO_TICKS(20)) != pdTRUE) deadline = xTaskGetTickCount();
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* NOTE: 首个任务先执行安全锁存；任何 NVS、网络及采集初始化均在其后。 */
    gs232b_motion_config_t config;
    gs232b_time_us_t reversal_us;
    gs232b_app_board_config(&config, &reversal_us);
    const gs232b_error_t error = gs232b_app_start(&runtime.app,
        gs232b_esp_app_platform(&runtime), &config, reversal_us);
    if (error != GS232B_OK) {
        return;
    }
    if (xTaskCreate(control, "gs_control", 10240, &runtime, 10, NULL) != pdPASS) {
        runtime.app.status.phase = GS232B_APP_FAILED;
        runtime.app.status.fatal_error = GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
}
