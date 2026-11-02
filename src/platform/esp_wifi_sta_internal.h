#ifndef GS232B_ESP_WIFI_STA_INTERNAL_H
#define GS232B_ESP_WIFI_STA_INTERNAL_H

#include "gs232b/esp_wifi_sta.h"
#include "esp_wifi.h"
#include "esp_timer.h"

enum { GS232B_STA_JOB_NONE, GS232B_STA_JOB_APPLY, GS232B_STA_JOB_STOP,
       GS232B_STA_JOB_AP_START, GS232B_STA_JOB_AP_STOP, GS232B_STA_JOB_EXIT };
ESP_EVENT_DECLARE_BASE(GS232B_STA_FENCE_EVENT);

/** @brief 转换 SDK 错误，保留原始值到 snapshot 由调用方负责。 */
gs232b_error_t gs232b_sta_error(esp_err_t error);
/** @brief 持有 lock 时清除运行地址并撤销 STA 会话，epoch 饱和时进入 ERROR。 */
void gs232b_sta_invalidate(gs232b_esp_wifi_sta_t *adapter);
/** @brief 持有 lock 时记录失败并安排下一次指数退避，不触及凭据。 */
void gs232b_sta_retry(gs232b_esp_wifi_sta_t *adapter, gs232b_wifi_sta_failure_t failure,
                       gs232b_error_t error, esp_err_t sdk_error, uint16_t reason);
/** @brief 工作任务独占：停止、排空并释放本 STA 资源；失败资源保留。 */
gs232b_error_t gs232b_sta_stop(gs232b_esp_wifi_sta_t *adapter);
/** @brief 工作任务独占：创建 STA netif、应用 RAM 凭据与 DHCP/静态 IPv4 并开始连接。 */
gs232b_error_t gs232b_sta_start(gs232b_esp_wifi_sta_t *adapter);
gs232b_error_t gs232b_sta_ap_start(gs232b_esp_wifi_sta_t *adapter);
gs232b_error_t gs232b_sta_ap_stop(gs232b_esp_wifi_sta_t *adapter);
/** @brief 事件回调仅更新 STA 状态，绝不直接重连。 */
void gs232b_sta_event(void *ctx, esp_event_base_t base, int32_t id, void *data);
/** @brief 默认循环屏障回调，隔离没有 netif 标识的旧 Wi-Fi 事件。 */
void gs232b_sta_fence_event(void *ctx, esp_event_base_t base, int32_t id, void *data);
#endif
