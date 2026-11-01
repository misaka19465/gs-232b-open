#ifndef TEST_ESP_ETH_NETIF_GLUE_H
#define TEST_ESP_ETH_NETIF_GLUE_H
#include "esp_eth.h"
typedef struct fake_eth_glue *esp_eth_netif_glue_handle_t;
esp_eth_netif_glue_handle_t esp_eth_new_netif_glue(esp_eth_handle_t);
esp_err_t esp_eth_del_netif_glue(esp_eth_netif_glue_handle_t);
#endif
