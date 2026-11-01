#ifndef TEST_ESP_ETH_PHY_H
#define TEST_ESP_ETH_PHY_H
#include "esp_err.h"
typedef struct esp_eth_phy_s esp_eth_phy_t;
struct esp_eth_phy_s { esp_err_t (*del)(esp_eth_phy_t *); };
#endif
