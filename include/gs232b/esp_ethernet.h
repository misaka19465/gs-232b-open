#ifndef GS232B_ESP_ETHERNET_H
#define GS232B_ESP_ETHERNET_H

#include "gs232b/ethernet.h"
#include "gs232b/w5500_reset.h"

#include <stdatomic.h>

#ifdef ESP_PLATFORM
#include "esp_eth.h"
#include "esp_eth_netif_glue.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* NOTE: 私有布局只供静态分配，不得复制活动对象。控制任务独占 reset、job、
 * closing；工作任务独占驱动资源；事件回调和快照通过 lock 共享 state。
 * 所有公开 API 均与输出管理器同一串行所有者调用，不得从事件回调重入。 */
typedef struct {
    gs232b_w5500_reset_t reset;
    gs232b_ethernet_snapshot_t state;
    gs232b_time_us_t waiting_since_us;
    uint64_t fence_serial;
    SemaphoreHandle_t lock;
    SemaphoreHandle_t fence;
    SemaphoreHandle_t stopped;
    QueueHandle_t requests;
    QueueHandle_t results;
    TaskHandle_t worker;
    esp_eth_mac_t *mac;
    esp_eth_phy_t *phy;
    esp_eth_handle_t driver;
    esp_netif_t *netif;
    esp_eth_netif_glue_handle_t glue;
    esp_event_handler_instance_t eth_handler;
    esp_event_handler_instance_t ip_handler;
    esp_event_handler_instance_t fence_handler;
    unsigned job;
    unsigned closing;
    bool start_attempted;
    bool accepting_events;
    atomic_bool cancelled;
    bool opened;
} gs232b_esp_ethernet_t;

/**
 * @brief 准备 W5500 生命周期服务和网络任务；NULL config 默认 DHCP，不操作输出。
 * @param adapter 清零的独占实例，生命周期覆盖所有异步工作和借用的 bus/outputs。
 * @param bus 已初始化、访问门关闭的共享总线。
 * @param outputs 已初始化并已知的唯一输出管理器。
 * @note 初始化/复用全局 esp_netif 和默认事件循环，关闭时不销毁它们。
 *       首次启动需 recover，随后持续 service；不在这里接入应用会话或持久化格式。
 */
gs232b_error_t gs232b_esp_ethernet_open(gs232b_esp_ethernet_t *adapter,
                                      gs232b_shared_spi_t *bus,
                                      gs232b_output_manager_t *outputs,
                                      const gs232b_ethernet_config_t *config);

/**
 * @brief 非阻塞请求启动或硬件恢复；输出 bit4 只由原有复位协调器提交。
 * @note 请求成功即使旧 ETH 会话失效；调用方处理 session_epoch 后再接纳输入。
 *       失败不自动重试，不覆盖输出故障；先由运动核心处理故障并显式全停。
 */
gs232b_error_t gs232b_esp_ethernet_recover(gs232b_esp_ethernet_t *adapter);

/**
 * @brief 校验并试用新配置，通过同一恢复流程重建 netif；进行中返回 BUSY。
 * @note 成功只表示请求接纳；通过 service/snapshot 确认结果，失败保留候选供显式
 *       重试或应用旧配置。配置事务、确认和存储由上层负责。
 */
gs232b_error_t gs232b_esp_ethernet_configure(gs232b_esp_ethernet_t *adapter,
                                           const gs232b_ethernet_config_t *config);

/**
 * @brief 非阻塞推进复位和 DHCP 超时；停启、探测及事件排空由工作任务执行。
 * @return OK 可表示等待中，WOULD_BLOCK 表示状态暂被事件任务更新；其他错误不自动重试。
 */
gs232b_error_t gs232b_esp_ethernet_service(gs232b_esp_ethernet_t *adapter);

/**
 * @brief 零等待复制配置与运行状态；竞争时返回 WOULD_BLOCK，输出清零。
 * @note session_epoch 饱和后保持 ERROR，须关闭再初始化。
 */
gs232b_error_t gs232b_esp_ethernet_snapshot(gs232b_esp_ethernet_t *adapter,
                                          gs232b_ethernet_snapshot_t *snapshot);

/**
 * @brief 异步停用/释放本接口；WOULD_BLOCK 时继续调用，成功后方可释放 bus/outputs。
 * @note 不操作方向或 bit4；释放失败保留资源，可重试，不强杀正在操作硬件的任务。
 */
gs232b_error_t gs232b_esp_ethernet_close(gs232b_esp_ethernet_t *adapter);

#endif
#endif
