#ifndef TEST_ESP_ETH_MAC_H
#define TEST_ESP_ETH_MAC_H
#include "esp_err.h"
typedef struct esp_eth_mac_s esp_eth_mac_t;
struct esp_eth_mac_s { esp_err_t (*del)(esp_eth_mac_t *); };
#endif
