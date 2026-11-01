#ifndef TEST_ESP_MAC_H
#define TEST_ESP_MAC_H
#include <stdint.h>
#include "esp_err.h"
typedef enum { ESP_MAC_ETH } esp_mac_type_t;
esp_err_t esp_read_mac(uint8_t *, esp_mac_type_t);
#endif
