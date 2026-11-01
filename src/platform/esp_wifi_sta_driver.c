#include "esp_wifi_sta_internal.h"

#include <string.h>
#include "esp_wifi_default.h"

ESP_EVENT_DEFINE_BASE(GS232B_STA_FENCE_EVENT);

typedef struct { gs232b_esp_wifi_sta_t *adapter; uint64_t serial; } fence_message_t;

void gs232b_sta_fence_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    (void)id;
    gs232b_esp_wifi_sta_t *adapter = ctx;
    const fence_message_t *message = data;
    if (message == NULL || message->adapter != adapter) { return; }
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    if (message->serial == adapter->fence_serial) { (void)xSemaphoreGive(adapter->fence); }
    (void)xSemaphoreGive(adapter->lock);
}

static gs232b_error_t sdk_result(gs232b_esp_wifi_sta_t *adapter, esp_err_t error)
{
    if (error != ESP_OK) {
        (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
        adapter->state.sdk_error = error;
        (void)xSemaphoreGive(adapter->lock);
    }
    return gs232b_sta_error(error);
}

static gs232b_error_t fence(gs232b_esp_wifi_sta_t *adapter)
{
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    (void)xSemaphoreTake(adapter->fence, 0);
    const fence_message_t message = {adapter, ++adapter->fence_serial};
    (void)xSemaphoreGive(adapter->lock);
    const esp_err_t error = esp_event_post(GS232B_STA_FENCE_EVENT, 0, &message,
                                          sizeof(message), pdMS_TO_TICKS(100));
    if (error != ESP_OK) { return sdk_result(adapter, error); }
    return sdk_result(adapter, xSemaphoreTake(adapter->fence, pdMS_TO_TICKS(1000)) == pdPASS ?
                       ESP_OK : ESP_ERR_TIMEOUT);
}

static gs232b_error_t unregister(gs232b_esp_wifi_sta_t *adapter, esp_event_base_t base,
                                 esp_event_handler_instance_t *handler)
{
    if (*handler == NULL) { return GS232B_OK; }
    const esp_err_t error = esp_event_handler_instance_unregister(base, ESP_EVENT_ANY_ID, *handler);
    if (error == ESP_OK) { *handler = NULL; }
    return sdk_result(adapter, error);
}

gs232b_error_t gs232b_sta_stop(gs232b_esp_wifi_sta_t *adapter)
{
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    adapter->accepting_events = false;
    gs232b_sta_invalidate(adapter);
    adapter->state.connected = false;
    (void)xSemaphoreGive(adapter->lock);
    if (adapter->start_attempted) {
        const esp_err_t error = esp_wifi_stop();
        if (error != ESP_OK && error != ESP_ERR_WIFI_NOT_STARTED) {
            return sdk_result(adapter, error);
        }
        adapter->start_attempted = false;
    }
    gs232b_error_t result = unregister(adapter, WIFI_EVENT, &adapter->wifi_handler);
    if (result != GS232B_OK) { return result; }
    result = unregister(adapter, IP_EVENT, &adapter->ip_handler);
    if (result != GS232B_OK) { return result; }
    /* NOTE: 先让默认 handlers 消费 STOP 及其派生 IP 事件，再卸载 netif 驱动。
     * Wi-Fi 事件不携带 netif，重建前必须排空整个旧驱动代次。 */
    result = fence(adapter);
    if (result != GS232B_OK) { return result; }
    if (adapter->initialized) {
        result = sdk_result(adapter, esp_wifi_deinit());
        if (result != GS232B_OK) { return result; }
        adapter->initialized = false;
    }
    if (adapter->defaults_attached) {
        result = sdk_result(adapter, esp_wifi_clear_default_wifi_driver_and_handlers(adapter->netif));
        if (result != GS232B_OK) { return result; }
        adapter->defaults_attached = false;
    }
    if (adapter->netif != NULL) {
        esp_netif_destroy(adapter->netif);
        adapter->netif = NULL;
    }
    return fence(adapter);
}

gs232b_error_t gs232b_sta_start(gs232b_esp_wifi_sta_t *adapter)
{
    if (atomic_load(&adapter->cancelled)) { return GS232B_OK; }
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    base.if_key = "GS232B_STA";
    base.route_prio = GS232B_ESP_WIFI_STA_ROUTE_PRIORITY;
    const esp_netif_config_t netif = {.base = &base, .stack = ESP_NETIF_NETSTACK_DEFAULT_WIFI_STA};
    adapter->netif = esp_netif_new(&netif);
    if (adapter->netif == NULL) { return sdk_result(adapter, ESP_ERR_NO_MEM); }
    /* NOTE: attach 失败仍可能留下 SDK 全局 netif 指针，清理路径也必须撤销它。 */
    adapter->defaults_attached = true;
    gs232b_error_t result = sdk_result(adapter, esp_netif_attach_wifi_station(adapter->netif));
    if (result != GS232B_OK) { return result; }
    result = sdk_result(adapter, esp_wifi_set_default_wifi_sta_handlers());
    if (result != GS232B_OK) { return result; }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = 0;
    result = sdk_result(adapter, esp_wifi_init(&init));
    if (result != GS232B_OK) { return result; }
    adapter->initialized = true;
    result = sdk_result(adapter, esp_wifi_set_storage(WIFI_STORAGE_RAM));
    if (result != GS232B_OK) { return result; }
    result = sdk_result(adapter, esp_wifi_set_mode(WIFI_MODE_STA));
    if (result != GS232B_OK) { return result; }
    wifi_config_t credentials = {0};
    memcpy(credentials.sta.ssid, adapter->configured.ssid, strlen(adapter->configured.ssid));
    memcpy(credentials.sta.password, adapter->configured.password, strlen(adapter->configured.password));
    credentials.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    credentials.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    credentials.sta.failure_retry_cnt = 0;
    result = sdk_result(adapter, esp_wifi_set_config(WIFI_IF_STA, &credentials));
    memset(&credentials, 0, sizeof(credentials));
    if (result != GS232B_OK) { return result; }
    uint8_t mac[6];
    result = sdk_result(adapter, esp_wifi_get_mac(WIFI_IF_STA, mac));
    if (result != GS232B_OK) { return result; }
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    memcpy(adapter->state.mac, mac, sizeof(mac));
    (void)xSemaphoreGive(adapter->lock);
    result = sdk_result(adapter, esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
        gs232b_sta_event, adapter, &adapter->wifi_handler));
    if (result != GS232B_OK) { return result; }
    result = sdk_result(adapter, esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
        gs232b_sta_event, adapter, &adapter->ip_handler));
    if (result != GS232B_OK) { return result; }
    adapter->start_attempted = true;
    result = sdk_result(adapter, esp_wifi_start());
    if (result != GS232B_OK) { return result; }
    result = fence(adapter);
    if (result != GS232B_OK || atomic_load(&adapter->cancelled)) { return result; }
    (void)xSemaphoreTake(adapter->lock, portMAX_DELAY);
    if (atomic_load(&adapter->cancelled) || adapter->state.session_epoch == UINT64_MAX) {
        (void)xSemaphoreGive(adapter->lock);
        return GS232B_OK;
    }
    const int64_t now = esp_timer_get_time();
    adapter->waiting_since_us = now < 0 ? 0 : (uint64_t)now;
    adapter->state.state = GS232B_WIFI_STA_CONNECTING;
    adapter->accepting_events = true;
    (void)xSemaphoreGive(adapter->lock);
    return sdk_result(adapter, esp_wifi_connect());
}
