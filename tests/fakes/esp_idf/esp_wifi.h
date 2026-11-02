#ifndef TEST_ESP_WIFI_H
#define TEST_ESP_WIFI_H
#include "esp_netif.h"
ESP_EVENT_DECLARE_BASE(WIFI_EVENT);
typedef enum { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
typedef enum { WIFI_IF_STA, WIFI_IF_AP } wifi_interface_t;
typedef enum { WIFI_STORAGE_FLASH, WIFI_STORAGE_RAM } wifi_storage_t;
typedef enum { WIFI_AUTH_OPEN, WIFI_AUTH_WPA2_PSK } wifi_auth_mode_t;
#define WIFI_ALL_CHANNEL_SCAN 1
#define WIFI_INIT_CONFIG_DEFAULT() {.nvs_enable = 1}
typedef struct { int nvs_enable; } wifi_init_config_t;
typedef struct {
    uint8_t ssid[32], password[64];
    struct { wifi_auth_mode_t authmode; } threshold;
    int scan_method;
    uint8_t failure_retry_cnt;
} wifi_sta_config_t;
typedef struct {
    uint8_t ssid[32], password[64], ssid_len, channel, authmode, ssid_hidden, max_connection;
} wifi_ap_config_t;
typedef struct { uint8_t ssid[33]; int8_t rssi; wifi_auth_mode_t authmode; } wifi_ap_record_t;
typedef union { wifi_sta_config_t sta; wifi_ap_config_t ap; } wifi_config_t;
typedef struct { uint8_t ssid[32], ssid_len, bssid[6], reason; int8_t rssi; }
    wifi_event_sta_disconnected_t;
enum { WIFI_EVENT_STA_START, WIFI_EVENT_STA_STOP, WIFI_EVENT_STA_CONNECTED, WIFI_EVENT_STA_DISCONNECTED };
enum { WIFI_REASON_AUTH_EXPIRE = 2, WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 15,
    WIFI_REASON_BEACON_TIMEOUT = 200, WIFI_REASON_NO_AP_FOUND = 201, WIFI_REASON_AUTH_FAIL = 202,
    WIFI_REASON_HANDSHAKE_TIMEOUT = 204, WIFI_REASON_CONNECTION_FAIL = 205,
    WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY = 210,
    WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD = 211, WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD = 212 };
#define ESP_ERR_WIFI_NOT_STARTED 0x3002
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_set_storage(wifi_storage_t);
esp_err_t esp_wifi_set_mode(wifi_mode_t);
esp_err_t esp_wifi_set_config(wifi_interface_t, wifi_config_t *);
esp_err_t esp_wifi_get_mac(wifi_interface_t, uint8_t *);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_scan_start(const void *, bool);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *, wifi_ap_record_t *);
esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *);
esp_err_t esp_wifi_clear_ap_list(void);
#endif
