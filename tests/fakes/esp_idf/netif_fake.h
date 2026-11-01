#ifndef TEST_NETIF_FAKE_H
#define TEST_NETIF_FAKE_H
#include "esp_netif.h"
struct fake_netif {
    esp_netif_ip_info_t ip;
    esp_netif_dns_info_t dns[2];
    esp_netif_dhcp_status_t dhcp;
    unsigned flags;
    bool up;
};
#endif
