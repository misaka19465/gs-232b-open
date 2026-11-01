#ifndef GS232B_WIFI_STA_H
#define GS232B_WIFI_STA_H

#include "gs232b/types.h"

#define GS232B_WIFI_STA_SSID_MAX 32U
#define GS232B_WIFI_STA_PASSWORD_MAX 64U
#define GS232B_WIFI_STA_CONNECT_TIMEOUT_US UINT64_C(30000000)
#define GS232B_WIFI_STA_DHCP_TIMEOUT_US UINT64_C(30000000)
#define GS232B_WIFI_STA_RETRY_MAX_US UINT64_C(30000000)

typedef struct {
    bool enabled;
    char ssid[GS232B_WIFI_STA_SSID_MAX + 1U];
    char password[GS232B_WIFI_STA_PASSWORD_MAX + 1U];
} gs232b_wifi_sta_config_t;

typedef enum {
    GS232B_WIFI_STA_DISABLED = 0,
    GS232B_WIFI_STA_STARTING,
    GS232B_WIFI_STA_CONNECTING,
    GS232B_WIFI_STA_DHCP_WAIT,
    GS232B_WIFI_STA_READY,
    GS232B_WIFI_STA_RETRY_WAIT,
    GS232B_WIFI_STA_ERROR
} gs232b_wifi_sta_state_t;

typedef enum {
    GS232B_WIFI_STA_FAILURE_NONE = 0,
    GS232B_WIFI_STA_FAILURE_AUTH,
    GS232B_WIFI_STA_FAILURE_NO_AP,
    GS232B_WIFI_STA_FAILURE_DISCONNECTED,
    GS232B_WIFI_STA_FAILURE_CONNECT_TIMEOUT,
    GS232B_WIFI_STA_FAILURE_DHCP_TIMEOUT,
    GS232B_WIFI_STA_FAILURE_DRIVER
} gs232b_wifi_sta_failure_t;

/* NOTE: 运行 IPv4 为主机序；Wi-Fi 独立 DHCP，不复用 Ethernet 静态配置。 */
typedef struct {
    uint32_t address, netmask, gateway, dns[2];
} gs232b_wifi_sta_ipv4_t;

typedef struct {
    gs232b_wifi_sta_state_t state;
    gs232b_wifi_sta_failure_t failure;
    gs232b_error_t last_error;
    int32_t sdk_error;
    uint16_t disconnect_reason;
    uint32_t consecutive_failures;
    gs232b_time_us_t retry_at_us;
    gs232b_time_us_t retry_delay_us;
    gs232b_wifi_sta_ipv4_t current;
    uint8_t mac[6];
    bool enabled, password_set, connected, ipv4_valid;
    char ssid[GS232B_WIFI_STA_SSID_MAX + 1U];
    /* NOTE: 本接口断线/失去地址/重配置时递增；上层据此撤销旧 STA 会话。 */
    uint64_t session_epoch;
} gs232b_wifi_sta_snapshot_t;

/** @brief 校验 UTF-8 SSID、WPA2 8..63 字节可打印 ASCII 口令或 64 位十六进制 PSK；禁用可无凭据。 */
bool gs232b_wifi_sta_config_valid(const gs232b_wifi_sta_config_t *config);
/** @brief 失败次数从 1 起对应 1/2/4/8/16/30 秒，后续保持 30 秒，不溢出。 */
gs232b_time_us_t gs232b_wifi_sta_retry_delay(uint32_t consecutive_failures);

#endif
