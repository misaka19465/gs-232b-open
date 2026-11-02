#include "wifi_api.h"
#include "esp_wifi_default.h"
#include "ethernet_api.h"
#include "netif_fake.h"

#include <assert.h>
#include <pthread.h>
#include <string.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t scan_condition = PTHREAD_COND_INITIALIZER;
static bool scan_pause_requested, scan_paused;
static fake_wifi_status_t status;
static esp_err_t faults[WIFI_FAKE_CALLS];
static esp_netif_t *station;
static esp_netif_t *ap;
static esp_event_handler_instance_t defaults, ap_defaults;
static wifi_ap_record_t scan_records[80];
static uint16_t scan_count, scan_cursor, scan_environment_count;

static esp_err_t call(fake_wifi_call_t which)
{
    assert(pthread_mutex_lock(&lock) == 0);
    ++status.calls[which];
    const esp_err_t error = faults[which];
    faults[which] = ESP_OK;
    assert(pthread_mutex_unlock(&lock) == 0);
    return error;
}

void fake_wifi_reset(void)
{
    assert(station == NULL && ap == NULL && defaults == NULL && ap_defaults == NULL);
    memset(&status, 0, sizeof(status));
    memset(faults, 0, sizeof(faults));
    scan_count = 0;
    scan_cursor = 0;
    scan_environment_count = 0;
    scan_pause_requested = scan_paused = false;
}

fake_wifi_status_t fake_wifi_status(void)
{
    assert(pthread_mutex_lock(&lock) == 0);
    const fake_wifi_status_t copy = status;
    assert(pthread_mutex_unlock(&lock) == 0);
    return copy;
}

void fake_wifi_fail(fake_wifi_call_t which, esp_err_t error)
{
    assert(pthread_mutex_lock(&lock) == 0);
    faults[which] = error;
    assert(pthread_mutex_unlock(&lock) == 0);
}

static void default_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)ctx;
    (void)base;
    (void)data;
    assert(station != NULL);
    if (id == WIFI_EVENT_STA_CONNECTED) {
        station->up = true;
        if (station->flags & ESP_NETIF_DHCP_CLIENT) station->dhcp = ESP_NETIF_DHCP_STARTED;
    } else if (id == WIFI_EVENT_STA_DISCONNECTED || id == WIFI_EVENT_STA_STOP) {
        station->up = false;
        station->dhcp = ESP_NETIF_DHCP_INIT;
        memset(&station->ip, 0, sizeof(station->ip));
        memset(station->dns, 0, sizeof(station->dns));
    }
}

static void default_ap_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)ctx; (void)base; (void)id; (void)data;
}

esp_err_t esp_netif_attach_wifi_station(esp_netif_t *netif)
{
    assert(station == NULL);
    station = netif;
    return call(WIFI_FAKE_ATTACH);
}

esp_err_t esp_netif_attach_wifi_ap(esp_netif_t *netif)
{
    assert(ap == NULL);
    ap = netif;
    return call(WIFI_FAKE_ATTACH);
}

esp_err_t esp_wifi_set_default_wifi_sta_handlers(void)
{
    const esp_err_t error = call(WIFI_FAKE_DEFAULTS);
    if (error != ESP_OK) { return error; }
    assert(defaults == NULL);
    return esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                default_event, NULL, &defaults);
}

esp_err_t esp_wifi_set_default_wifi_ap_handlers(void)
{
    const esp_err_t error = call(WIFI_FAKE_DEFAULTS);
    if (error != ESP_OK) return error;
    assert(ap_defaults == NULL);
    return esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
        default_ap_event, NULL, &ap_defaults);
}

esp_err_t esp_wifi_clear_default_wifi_driver_and_handlers(void *netif)
{
    const esp_err_t error = call(WIFI_FAKE_CLEAR);
    if (error != ESP_OK) { return error; }
    assert(station == netif || ap == netif);
    if (station == netif && defaults != NULL) {
        const esp_err_t unregistered = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, defaults);
        if (unregistered != ESP_OK) { return unregistered; }
        defaults = NULL;
    }
    if (ap == netif && ap_defaults != NULL) {
        const esp_err_t unregistered = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, ap_defaults);
        if (unregistered != ESP_OK) return unregistered;
        ap_defaults = NULL;
    }
    if (station == netif) station = NULL;
    if (ap == netif) ap = NULL;
    return ESP_OK;
}

esp_err_t esp_wifi_init(const wifi_init_config_t *config)
{
    assert(config->nvs_enable == 0);
    const esp_err_t error = call(WIFI_FAKE_INIT);
    if (error != ESP_OK) { return error; }
    assert(pthread_mutex_lock(&lock) == 0);
    assert(!status.initialized);
    status.initialized = true;
    assert(pthread_mutex_unlock(&lock) == 0);
    return ESP_OK;
}

esp_err_t esp_wifi_deinit(void)
{
    const esp_err_t error = call(WIFI_FAKE_DEINIT);
    if (error != ESP_OK) { return error; }
    assert(pthread_mutex_lock(&lock) == 0);
    assert(status.initialized && !status.started);
    status.initialized = false;
    assert(pthread_mutex_unlock(&lock) == 0);
    return ESP_OK;
}

esp_err_t esp_wifi_set_storage(wifi_storage_t storage)
{
    assert(storage == WIFI_STORAGE_RAM);
    return call(WIFI_FAKE_STORAGE);
}

esp_err_t esp_wifi_set_mode(wifi_mode_t mode)
{
    assert(mode == WIFI_MODE_STA || mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    const esp_err_t error = call(WIFI_FAKE_MODE);
    if (error == ESP_OK) status.mode = mode;
    return error;
}

esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *config)
{
    assert((interface == WIFI_IF_STA &&
            config->sta.threshold.authmode == (config->sta.password[0] == '\0' ?
                WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK)) || interface == WIFI_IF_AP);
    if (interface == WIFI_IF_STA) assert(config->sta.failure_retry_cnt == 0);
    const esp_err_t error = call(WIFI_FAKE_CONFIG);
    if (error != ESP_OK) { return error; }
    assert(pthread_mutex_lock(&lock) == 0);
    status.credentials = *config;
    assert(pthread_mutex_unlock(&lock) == 0);
    return ESP_OK;
}

esp_err_t esp_wifi_get_mac(wifi_interface_t interface, uint8_t *mac)
{
    assert(interface == WIFI_IF_STA || interface == WIFI_IF_AP);
    const esp_err_t error = call(WIFI_FAKE_MAC);
    if (error == ESP_OK) { memcpy(mac, "\x02\x00\x00\x00\x00\x11", 6); }
    return error;
}

esp_err_t esp_wifi_start(void)
{
    const esp_err_t error = call(WIFI_FAKE_START);
    assert(pthread_mutex_lock(&lock) == 0);
    assert(status.initialized);
    status.started = true;
    assert(pthread_mutex_unlock(&lock) == 0);
    if (error != ESP_OK) { return error; }
    return esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_START, NULL, 0, 0);
}

esp_err_t esp_wifi_stop(void)
{
    const esp_err_t error = call(WIFI_FAKE_STOP);
    if (error != ESP_OK) { return error; }
    assert(pthread_mutex_lock(&lock) == 0);
    status.started = false;
    assert(pthread_mutex_unlock(&lock) == 0);
    return esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_STOP, NULL, 0, 0);
}

esp_err_t esp_wifi_connect(void)
{
    assert(fake_wifi_status().started);
    return call(WIFI_FAKE_CONNECT);
}

esp_err_t esp_wifi_scan_start(const void *config, bool block)
{
    (void)config; (void)block;
    const esp_err_t error = call(WIFI_FAKE_SCAN_START);
    assert(pthread_mutex_lock(&lock) == 0);
    scan_paused = scan_pause_requested;
    while (scan_pause_requested) assert(pthread_cond_wait(&scan_condition, &lock) == 0);
    scan_paused = false;
    assert(pthread_mutex_unlock(&lock) == 0);
    scan_count = scan_environment_count;
    scan_cursor = 0;
    return error;
}

void fake_wifi_scan_pause(bool pause)
{
    assert(pthread_mutex_lock(&lock) == 0);
    scan_pause_requested = pause;
    assert(pthread_cond_broadcast(&scan_condition) == 0);
    assert(pthread_mutex_unlock(&lock) == 0);
}

bool fake_wifi_scan_paused(void)
{
    assert(pthread_mutex_lock(&lock) == 0);
    const bool paused = scan_paused;
    assert(pthread_mutex_unlock(&lock) == 0);
    return paused;
}

void fake_wifi_scan_results(const wifi_ap_record_t *records, uint16_t count)
{
    assert(count <= 80);
    memcpy(scan_records, records, count * sizeof(*records));
    scan_count = count;
    scan_environment_count = count;
    for (uint16_t i = 1; i < count; ++i) {
        const wifi_ap_record_t record = scan_records[i];
        uint16_t at = i;
        while (at != 0 && scan_records[at - 1U].rssi < record.rssi) {
            scan_records[at] = scan_records[at - 1U];
            --at;
        }
        scan_records[at] = record;
    }
}

esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count)
{
    if (count == NULL) return ESP_ERR_INVALID_ARG;
    *count = scan_count;
    return call(WIFI_FAKE_SCAN_COUNT);
}

esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *records)
{
    if (count == NULL || (records == NULL && *count != 0)) return ESP_ERR_INVALID_ARG;
    if (*count > scan_count) *count = scan_count;
    memcpy(records, scan_records, *count * sizeof(*records));
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *record)
{
    if (record == NULL || scan_cursor >= scan_count) return ESP_ERR_INVALID_ARG;
    const esp_err_t error = call(WIFI_FAKE_SCAN_RECORD);
    if (error == ESP_OK) *record = scan_records[scan_cursor++];
    return error;
}

esp_err_t esp_wifi_clear_ap_list(void)
{
    const esp_err_t error = call(WIFI_FAKE_SCAN_CLEAR);
    if (error == ESP_OK) scan_count = scan_cursor = 0;
    return error;
}

void fake_wifi_connected(void)
{
    assert(esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL, 0, 0) == ESP_OK);
    fake_eth_flush();
}

void fake_wifi_disconnected(uint8_t reason)
{
    const wifi_event_sta_disconnected_t event = {.reason = reason};
    assert(esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event, sizeof(event), 0) == ESP_OK);
    fake_eth_flush();
}

void fake_wifi_lease(gs232b_esp_wifi_sta_t *adapter, gs232b_wifi_sta_ipv4_t ip)
{
    assert(adapter->netif == station && station->dhcp == ESP_NETIF_DHCP_STARTED);
    station->ip = (esp_netif_ip_info_t){.ip.addr = esp_netif_htonl(ip.address),
        .netmask.addr = esp_netif_htonl(ip.netmask), .gw.addr = esp_netif_htonl(ip.gateway)};
    for (unsigned i = 0; i < 2; ++i) { station->dns[i].ip.u_addr.ip4.addr = esp_netif_htonl(ip.dns[i]); }
    const ip_event_got_ip_t event = {.esp_netif = station, .ip_info = station->ip};
    assert(esp_event_post(IP_EVENT, IP_EVENT_STA_GOT_IP, &event, sizeof(event), 0) == ESP_OK);
    fake_eth_flush();
}

void fake_wifi_lost_ip(gs232b_esp_wifi_sta_t *adapter)
{
    memset(&station->ip, 0, sizeof(station->ip));
    const ip_event_got_ip_t event = {.esp_netif = adapter->netif};
    assert(esp_event_post(IP_EVENT, IP_EVENT_STA_LOST_IP, &event, sizeof(event), 0) == ESP_OK);
    fake_eth_flush();
}
