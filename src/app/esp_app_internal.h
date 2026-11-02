#ifndef GS232B_ESP_APP_INTERNAL_H
#define GS232B_ESP_APP_INTERNAL_H

#include "gs232b/app.h"
#include "gs232b/esp_adc.h"
#include "gs232b/esp_ethernet.h"
#include "gs232b/esp_shared_spi.h"
#include "gs232b/esp_store_worker.h"
#include "gs232b/esp_uart.h"
#include "gs232b/esp_wifi_sta.h"
#include "gs232b/esp_ws.h"
#include "esp_timer.h"

typedef struct {
    bool enabled;
    gs232b_provisioning_ap_config_t config;
} gs232b_app_ap_request_t;

typedef struct {
    gs232b_app_t app;
    gs232b_esp_shared_spi_t spi;
    gs232b_esp_store_worker_t storage;
    gs232b_esp_adc_t adc;
    gs232b_esp_uart_t uart;
    gs232b_esp_ethernet_t eth;
    gs232b_esp_wifi_sta_t wifi;
    gs232b_esp_ws_t ws;
    TaskHandle_t sampling_task;
    esp_timer_handle_t sample_timer;
    QueueHandle_t network_requests, ap_requests;
    gs232b_net_config_t desired, applied;
    gs232b_app_ap_request_t ap;
    gs232b_error_t eth_error, wifi_error, ws_error, bind_error;
    uint64_t ap_epoch;
    uint32_t ap_address;
    bool ap_pending;
    uint8_t uart_line[GS232B_LINE_STORAGE_BYTES];
} gs232b_esp_app_t;

/** @brief 构造真实板级端口；组合层与控制任务借用同一静态实例。 */
gs232b_app_platform_t gs232b_esp_app_platform(gs232b_esp_app_t *runtime);
/** @brief 在独立采集任务执行 ADC 转换，5ms timer 只投递可合并通知。 */
gs232b_error_t gs232b_app_adc_open(void *ctx, gs232b_sampler_t *sampler);
/** @brief 连接网络状态/应用/AP 邮箱，在任何会话打开之前恢复已确认网络记录。 */
gs232b_error_t gs232b_app_network_bind(void *ctx, gs232b_app_t *app);
/** @brief 独立启动 ETH、统一 AP+STA 及 HTTPD；失败不短路其他接口。 */
gs232b_error_t gs232b_app_network_start(void *ctx, gs232b_app_t *app);
/** @brief 有界消费网络请求、推进生命周期、发布接口代次，再服务 WS。 */
gs232b_error_t gs232b_app_network_service(void *ctx, gs232b_app_t *app);
/** @brief 应用已接纳的配置与 AP 请求；忙时留存最新完整请求。 */
void gs232b_app_network_requests(gs232b_esp_app_t *runtime);
/** @brief 发布真实可用 netif 及代次；竞争时撤销接口，不能保留已失效的控制连接。 */
bool gs232b_app_network_publish(gs232b_esp_app_t *runtime);

#endif
