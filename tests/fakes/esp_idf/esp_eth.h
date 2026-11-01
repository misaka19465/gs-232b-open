#ifndef TEST_ESP_ETH_H
#define TEST_ESP_ETH_H

#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_event.h"

typedef struct fake_eth_driver *esp_eth_handle_t;
typedef enum { ETH_CMD_S_MAC_ADDR } esp_eth_io_cmd_t;
typedef enum { ETHERNET_EVENT_START, ETHERNET_EVENT_STOP, ETHERNET_EVENT_CONNECTED,
    ETHERNET_EVENT_DISCONNECTED } eth_event_t;
typedef struct { esp_eth_mac_t *mac; esp_eth_phy_t *phy; } esp_eth_config_t;
#define ETH_DEFAULT_CONFIG(mac_, phy_) { .mac = (mac_), .phy = (phy_) }
ESP_EVENT_DECLARE_BASE(ETH_EVENT);

esp_err_t esp_eth_driver_install(const esp_eth_config_t *, esp_eth_handle_t *);
esp_err_t esp_eth_driver_uninstall(esp_eth_handle_t);
esp_err_t esp_eth_start(esp_eth_handle_t);
esp_err_t esp_eth_stop(esp_eth_handle_t);
esp_err_t esp_eth_ioctl(esp_eth_handle_t, esp_eth_io_cmd_t, void *);

#endif
