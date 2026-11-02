/*
 * GS-232B Open - rotator controller firmware.
 * Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef GS232B_ESP_WIFI_STA_H
#define GS232B_ESP_WIFI_STA_H

#include "gs232b/wifi_sta.h"
#include "gs232b/provisioning.h"
#include "gs232b/esp_network_event.h"

#define GS232B_ESP_WIFI_STA_ROUTE_PRIORITY 100

#ifdef ESP_PLATFORM
#include <stdatomic.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* NOTE: 私有布局供静态分配，不复制活动对象；管理 API 由同一控制任务串行调用。
 * 工作任务独占 SDK 生命周期和同步扫描；状态通过 lock 共享，不依赖 Ethernet。
 * SDK 回调仅零等待投递固定事件，service 有界消费后才更新快照。 */
typedef struct {
    gs232b_wifi_sta_config_t configured;
    gs232b_wifi_sta_snapshot_t state;
    gs232b_time_us_t waiting_since_us;
    SemaphoreHandle_t lock, stopped;
    QueueHandle_t fence;
    QueueHandle_t requests, results;
    QueueHandle_t events;
    atomic_uint event_generation;
    atomic_bool event_lost;
    TaskHandle_t worker;
    esp_netif_t *netif;
    esp_netif_t *ap_netif;
    gs232b_provisioning_ap_config_t ap_config;
    esp_event_handler_instance_t wifi_handler, ip_handler, fence_handler;
    uint64_t fence_serial;
    unsigned job, closing;
    bool initialized, start_attempted, defaults_attached, ap_defaults_attached;
    bool ap_requested, ap_started, accepting_events, opened;
    gs232b_provisioning_ap_t scan_aps[GS232B_PROVISIONING_SCAN_MAX_APS];
    size_t scan_count;
    gs232b_error_t scan_error;
    bool scan_truncated, scan_done;
    atomic_bool cancelled;
} gs232b_esp_wifi_sta_t;

/**
 * @brief 建立异步 STA 服务；NULL config 禁用且无凭据。成功后持续调用 service。
 * @param adapter 清零的独占实例，寿命覆盖 close 完成。
 * @note 独占统一 Wi-Fi 驱动所有权；STA 与配网 SoftAP 共用本实例，不能由其他 owner 控制。
 *       复用全局事件循环，close 不删除它；凭据仅应用到 RAM，不直接读写 NVS。
 */
gs232b_error_t gs232b_esp_wifi_sta_open(gs232b_esp_wifi_sta_t *adapter,
                                      const gs232b_wifi_sta_config_t *config);
/** @brief 接纳完整新配置；异步停用并排空旧代次，忙时返回 BUSY；不持久化或清除凭据。 */
gs232b_error_t gs232b_esp_wifi_sta_configure(gs232b_esp_wifi_sta_t *adapter,
                                           const gs232b_wifi_sta_config_t *config);
/** @brief 保留 SSID/密码，仅切换启用状态；无凭据时不可启用。 */
gs232b_error_t gs232b_esp_wifi_sta_set_enabled(gs232b_esp_wifi_sta_t *adapter, bool enabled);
/** @brief 非阻塞消费工作结果、推进连接/DHCP 超时与有界退避；SDK 操作在工作任务执行。 */
gs232b_error_t gs232b_esp_wifi_sta_service(gs232b_esp_wifi_sta_t *adapter);
/** @brief 零等待复制脱敏运行状态；竞争时返回 WOULD_BLOCK 并清零输出，不含密码。 */
gs232b_error_t gs232b_esp_wifi_sta_snapshot(gs232b_esp_wifi_sta_t *adapter,
                                          gs232b_wifi_sta_snapshot_t *snapshot);
/** @brief 控制任务判断工作任务是否正重建 netif；扫描期间接口仍稳定可用。 */
bool gs232b_esp_wifi_sta_interfaces_stable(const gs232b_esp_wifi_sta_t *adapter);
/** @brief 异步释放本 STA；WOULD_BLOCK 时继续调用，失败保留资源可重试，成功后才可释放实例。 */
gs232b_error_t gs232b_esp_wifi_sta_close(gs232b_esp_wifi_sta_t *adapter);

/** @brief Queue AP start on the same Wi-Fi owner; the AP uses the supplied static subnet. */
gs232b_error_t gs232b_esp_wifi_sta_ap_start(gs232b_esp_wifi_sta_t *adapter,
    const gs232b_provisioning_ap_config_t *config);
/** @brief Queue AP stop without clearing STA credentials, trial state, or recovery state. */
gs232b_error_t gs232b_esp_wifi_sta_ap_stop(gs232b_esp_wifi_sta_t *adapter);
/** @brief Build a provisioning AP port backed by this unified AP+STA adapter. */
gs232b_provisioning_ap_port_t gs232b_esp_wifi_sta_ap_port(gs232b_esp_wifi_sta_t *adapter);
/** @brief 工作任务同步扫描并复制最多 capacity 个有效 UTF-8 AP；不得从运动循环调用。 */
gs232b_error_t gs232b_esp_wifi_sta_ap_scan(gs232b_esp_wifi_sta_t *adapter,
    gs232b_provisioning_ap_t *aps, size_t capacity, size_t *count, bool *truncated);
/** @brief 请求工作任务扫描；完成前返回 WOULD_BLOCK，service 后重复调用取得结果。 */
gs232b_error_t gs232b_esp_wifi_sta_ap_scan_async(gs232b_esp_wifi_sta_t *adapter,
    gs232b_provisioning_ap_t *aps, size_t capacity, size_t *count, bool *truncated);
#endif
#endif
