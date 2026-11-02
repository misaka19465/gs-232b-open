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

#include "esp_ethernet_internal.h"
#include "gs232b/esp_w5500.h"

ESP_EVENT_DEFINE_BASE(GS232B_ETH_FENCE_EVENT);

typedef struct {
    gs232b_esp_ethernet_t *adapter;
    uint64_t serial;
} fence_message_t;

void gs232b_eth_fence_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    (void)id;
    gs232b_esp_ethernet_t *adapter = ctx;
    const fence_message_t *message = data;
    if (message == NULL || message->adapter != adapter) { return; }
    (void)xQueueSend(adapter->fence, &message->serial, 0);
}

static gs232b_error_t fence(gs232b_esp_ethernet_t *adapter)
{
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    (void)xQueueReset(adapter->fence);
    const fence_message_t message = {adapter, ++adapter->fence_serial};
    (void)xSemaphoreGive(adapter->lock);
    const esp_err_t error = esp_event_post(GS232B_ETH_FENCE_EVENT, 0,
        &message, sizeof(message), pdMS_TO_TICKS(100));
    if (error != ESP_OK) { return gs232b_eth_error(error); }
    uint64_t acknowledged = 0;
    return xQueueReceive(adapter->fence, &acknowledged, pdMS_TO_TICKS(1000)) == pdPASS &&
        acknowledged == message.serial ? GS232B_OK : GS232B_ERROR_TIMEOUT;
}

static gs232b_error_t unregister_handler(esp_event_base_t base,
                                        esp_event_handler_instance_t *handler)
{
    if (*handler == NULL) { return GS232B_OK; }
    const esp_err_t error = esp_event_handler_instance_unregister(
        base, ESP_EVENT_ANY_ID, *handler);
    if (error == ESP_OK) { *handler = NULL; }
    return gs232b_eth_error(error);
}

gs232b_error_t gs232b_eth_driver_quiesce(gs232b_esp_ethernet_t *adapter)
{
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    adapter->accepting_events = false;
    gs232b_eth_invalidate(adapter);
    adapter->state.link_up = false;
    (void)xSemaphoreGive(adapter->lock);
    if (adapter->driver != NULL && adapter->start_attempted) {
        /* NOTE: start 可在内部已切换 FSM 后失败，仍须尝试 stop；INVALID_STATE
         * 表示已经停止，其他错误不假定设备已排空，也不允许协调器继续复位。 */
        const esp_err_t error = esp_eth_stop(adapter->driver);
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
            return gs232b_eth_error(error);
        }
        adapter->start_attempted = false;
    }
    gs232b_error_t result = unregister_handler(ETH_EVENT, &adapter->eth_handler);
    if (result != GS232B_OK) { return result; }
    result = unregister_handler(IP_EVENT, &adapter->ip_handler);
    if (result != GS232B_OK) { return result; }
    if (adapter->glue != NULL) {
        /* NOTE: stop 只投递异步事件。屏障保证默认 glue 已处理所有先前的
         * START/STOP/link 事件，再卸载 glue/netif；不删除全局事件循环。 */
        result = fence(adapter);
        if (result != GS232B_OK) { return result; }
        const esp_err_t error = esp_eth_del_netif_glue(adapter->glue);
        if (error != ESP_OK) { return gs232b_eth_error(error); }
        adapter->glue = NULL;
    }
    if (adapter->netif != NULL) {
        esp_netif_action_stop(adapter->netif, ETH_EVENT, ETHERNET_EVENT_STOP, NULL);
    }
    if (adapter->driver != NULL) {
        const esp_err_t error = esp_eth_driver_uninstall(adapter->driver);
        if (error != ESP_OK) { return gs232b_eth_error(error); }
        adapter->driver = NULL;
    }
    if (adapter->phy != NULL) {
        const esp_err_t error = adapter->phy->del(adapter->phy);
        if (error != ESP_OK) { return gs232b_eth_error(error); }
        adapter->phy = NULL;
    }
    if (adapter->mac != NULL) {
        const esp_err_t error = adapter->mac->del(adapter->mac);
        if (error != ESP_OK) { return gs232b_eth_error(error); }
        adapter->mac = NULL;
    }
    /* NOTE: MAC 删除才会终止官方 RX 任务，netif 的寿命必须覆盖整个删除过程。 */
    if (adapter->netif != NULL) {
        esp_netif_destroy(adapter->netif);
        adapter->netif = NULL;
    }
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    adapter->state.dhcp_running = false;
    (void)xSemaphoreGive(adapter->lock);
    /* NOTE: 排空 STOP 回调间接排队的 IP 事件，避免新 netif 复用地址后误收旧事件。 */
    return fence(adapter);
}

static gs232b_error_t apply_ipv4(gs232b_esp_ethernet_t *adapter,
                                 const gs232b_ethernet_config_t *config)
{
    /* NOTE: 两种模式都先显式停 DHCP，再清空地址；新 netif 的 DNS 初始为零。
     * SDK 6.1 的 set_dns_info 拒绝零地址，可选 DNS 依靠重建 netif 清除。静态 netif 不带
     * DHCP_CLIENT 标志，防止 glue 在后续 link up 时自动启动 DHCP。 */
    esp_err_t error = esp_netif_dhcpc_stop(adapter->netif);
    if (error != ESP_OK && error != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return gs232b_eth_error(error);
    }
    const esp_netif_ip_info_t empty = {0};
    error = esp_netif_set_ip_info(adapter->netif, &empty);
    if (error != ESP_OK) { return gs232b_eth_error(error); }
    if (config->mode == GS232B_ETHERNET_DHCP) {
        /* NOTE: netif 尚未 up，start 将状态重置为 INIT；glue 在 link up 时
         * 启动真正 DHCP client，此时运行地址仍为零。 */
        return gs232b_eth_error(esp_netif_dhcpc_start(adapter->netif));
    }
    const esp_netif_ip_info_t ip = {
        .ip.addr = esp_netif_htonl(config->ipv4.address),
        .netmask.addr = esp_netif_htonl(config->ipv4.netmask),
        .gw.addr = esp_netif_htonl(config->ipv4.gateway)
    };
    error = esp_netif_set_ip_info(adapter->netif, &ip);
    if (error != ESP_OK) { return gs232b_eth_error(error); }
    for (unsigned i = 0; i < 2; ++i) {
        if (config->ipv4.dns[i] == 0) { continue; }
        esp_netif_dns_info_t dns = {.ip.type = ESP_IPADDR_TYPE_V4};
        dns.ip.u_addr.ip4.addr = esp_netif_htonl(config->ipv4.dns[i]);
        error = esp_netif_set_dns_info(adapter->netif, (esp_netif_dns_type_t)i, &dns);
        if (error != ESP_OK) { return gs232b_eth_error(error); }
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_eth_driver_resume(gs232b_esp_ethernet_t *adapter)
{
    if (adapter->driver != NULL || adapter->netif != NULL || adapter->mac != NULL ||
        adapter->phy != NULL || adapter->glue != NULL) { return GS232B_ERROR_BUSY; }
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    const gs232b_ethernet_config_t config = adapter->state.configured;
    (void)xSemaphoreGive(adapter->lock);
    gs232b_error_t result = gs232b_esp_w5500_create(adapter->reset.bus,
                                                  &adapter->mac, &adapter->phy);
    if (result != GS232B_OK) { return result; }
    const esp_eth_config_t driver = ETH_DEFAULT_CONFIG(adapter->mac, adapter->phy);
    esp_err_t error = esp_eth_driver_install(&driver, &adapter->driver);
    if (error != ESP_OK) { result = gs232b_eth_error(error); goto fail; }
    error = esp_eth_ioctl(adapter->driver, ETH_CMD_S_MAC_ADDR, adapter->state.mac);
    if (error != ESP_OK) { result = gs232b_eth_error(error); goto fail; }
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.if_key = "GS232B_ETH";
    base.route_prio = GS232B_ESP_ETHERNET_ROUTE_PRIORITY;
    if (config.mode == GS232B_ETHERNET_STATIC) { base.flags &= ~ESP_NETIF_DHCP_CLIENT; }
    const esp_netif_config_t netif = {
        .base = &base, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
    };
    adapter->netif = esp_netif_new(&netif);
    if (adapter->netif == NULL) { result = GS232B_ERROR_RESOURCE_EXHAUSTED; goto fail; }
    adapter->glue = esp_eth_new_netif_glue(adapter->driver);
    if (adapter->glue == NULL) { result = GS232B_ERROR_RESOURCE_EXHAUSTED; goto fail; }
    error = esp_netif_attach(adapter->netif, adapter->glue);
    if (error != ESP_OK) { result = gs232b_eth_error(error); goto fail; }
    result = apply_ipv4(adapter, &config);
    if (result != GS232B_OK) { goto fail; }
    /* NOTE: 注册在 glue 之后，事件回调才能读取已完成默认动作的 netif 状态。 */
    error = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID,
        gs232b_eth_event, adapter, &adapter->eth_handler);
    if (error != ESP_OK) { result = gs232b_eth_error(error); goto fail; }
    error = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
        gs232b_eth_event, adapter, &adapter->ip_handler);
    if (error != ESP_OK) { result = gs232b_eth_error(error); goto fail; }
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    if (atomic_load(&adapter->cancelled)) {
        (void)xSemaphoreGive(adapter->lock);
        result = GS232B_ERROR_NOT_READY;
        goto fail;
    }
    adapter->state.state = GS232B_ETHERNET_LINK_DOWN;
    adapter->state.last_error = GS232B_OK;
    adapter->state.dhcp_running = false;
    (void)xQueueReset(adapter->events);
    atomic_store(&adapter->event_lost, false);
    atomic_fetch_add(&adapter->event_generation, 1);
    adapter->accepting_events = true;
    (void)xSemaphoreGive(adapter->lock);
    adapter->start_attempted = true;
    error = esp_eth_start(adapter->driver);
    if (error == ESP_OK) { return GS232B_OK; }
    result = gs232b_eth_error(error);
fail:
    /* NOTE: 清理失败保留句柄；下次显式恢复先重新 quiesce，不复用旧寄存器配置。 */
    (void)gs232b_eth_driver_quiesce(adapter);
    return result;
}
