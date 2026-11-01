#ifndef TEST_ESP_NETIF_H
#define TEST_ESP_NETIF_H

#include <stdbool.h>
#include "esp_event.h"

typedef struct fake_netif esp_netif_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { union { esp_ip4_addr_t ip4; } u_addr; unsigned type; } esp_ip_addr_t;
typedef struct { esp_ip4_addr_t ip, netmask, gw; } esp_netif_ip_info_t;
typedef struct { esp_ip_addr_t ip; } esp_netif_dns_info_t;
typedef enum { ESP_NETIF_DNS_MAIN, ESP_NETIF_DNS_BACKUP } esp_netif_dns_type_t;
typedef enum { ESP_NETIF_DHCP_INIT, ESP_NETIF_DHCP_STARTED, ESP_NETIF_DHCP_STOPPED }
    esp_netif_dhcp_status_t;
typedef struct { esp_netif_t *esp_netif; esp_netif_ip_info_t ip_info; bool ip_changed; }
    ip_event_got_ip_t;
typedef struct { unsigned flags; const char *if_key; int route_prio; }
    esp_netif_inherent_config_t;
typedef struct { const esp_netif_inherent_config_t *base; const void *driver, *stack; }
    esp_netif_config_t;

ESP_EVENT_DECLARE_BASE(IP_EVENT);
#define IP_EVENT_ETH_GOT_IP 100
#define IP_EVENT_ETH_LOST_IP 101
#define ESP_IPADDR_TYPE_V4 0
#define ESP_NETIF_DHCP_CLIENT 1U
#define ESP_NETIF_INHERENT_DEFAULT_ETH() { .flags = ESP_NETIF_DHCP_CLIENT, \
    .if_key = "ETH_DEF", .route_prio = 50 }
#define ESP_NETIF_NETSTACK_DEFAULT_ETH ((void *)1)
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED 0x5001
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED 0x5002
#define ESP_ERR_ESP_NETIF_INVALID_PARAMS 0x5003
#define ESP_ERR_ESP_NETIF_DHCPC_START_FAILED 0x5004

static inline uint32_t esp_netif_htonl(uint32_t value)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return value;
#else
    return ((value & 255U) << 24) | ((value & 65280U) << 8) |
           ((value >> 8) & 65280U) | (value >> 24);
#endif
}

esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_new(const esp_netif_config_t *);
void esp_netif_destroy(esp_netif_t *);
esp_err_t esp_netif_attach(esp_netif_t *, void *);
esp_err_t esp_netif_set_ip_info(esp_netif_t *, const esp_netif_ip_info_t *);
esp_err_t esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *);
esp_err_t esp_netif_set_dns_info(esp_netif_t *, esp_netif_dns_type_t, esp_netif_dns_info_t *);
esp_err_t esp_netif_get_dns_info(esp_netif_t *, esp_netif_dns_type_t, esp_netif_dns_info_t *);
esp_err_t esp_netif_dhcpc_stop(esp_netif_t *);
esp_err_t esp_netif_dhcpc_start(esp_netif_t *);
esp_err_t esp_netif_dhcpc_get_status(esp_netif_t *, esp_netif_dhcp_status_t *);
void esp_netif_action_stop(void *, esp_event_base_t, int32_t, void *);

#endif
