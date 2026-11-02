#ifndef GS232B_ESP_ETHERNET_INTERNAL_H
#define GS232B_ESP_ETHERNET_INTERNAL_H

#include "gs232b/esp_ethernet.h"

enum { GS232B_ETH_JOB_NONE, GS232B_ETH_JOB_QUIESCE, GS232B_ETH_JOB_RESUME,
       GS232B_ETH_JOB_EXIT };

/** @brief 转换 ESP 错误；内部调用不持有 state 锁执行 SDK 生命周期 API。 */
gs232b_error_t gs232b_eth_error(esp_err_t error);
/** @brief 在持有 state 锁时撤销运行地址及旧会话；不操作硬件或控制核心。 */
void gs232b_eth_invalidate(gs232b_esp_ethernet_t *adapter);
/** @brief 在持有 state 锁时记录错误并撤销地址。 */
void gs232b_eth_fail(gs232b_esp_ethernet_t *adapter, gs232b_error_t error);
/** @brief 工作任务独占：停用、排空并销毁本接口，保留释放失败的资源。 */
gs232b_error_t gs232b_eth_driver_quiesce(gs232b_esp_ethernet_t *adapter);
/** @brief 工作任务独占：在访问门打开且复位等待完成后重新安装驱动。 */
gs232b_error_t gs232b_eth_driver_resume(gs232b_esp_ethernet_t *adapter);
/** @brief 默认事件循环回调；只更新本接口的状态。 */
void gs232b_eth_event(void *ctx, esp_event_base_t base, int32_t id, void *data);
/** @brief 控制任务有界消费 SDK 事件；不会在网络生命周期工作未完成时访问 netif。 */
void gs232b_eth_events_service(gs232b_esp_ethernet_t *adapter);
/** @brief 默认循环屏障回调，数据为包含 adapter 和请求序号的值对象。 */
void gs232b_eth_fence_event(void *ctx, esp_event_base_t base, int32_t id, void *data);

ESP_EVENT_DECLARE_BASE(GS232B_ETH_FENCE_EVENT);

#endif
