#ifndef GS232B_ESP_W5500_H
#define GS232B_ESP_W5500_H

#include "gs232b/shared_spi.h"

#ifdef ESP_PLATFORM
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"

/**
 * @brief 创建官方 W5500 MAC/PHY，SPI 强制走共享适配器，reset_gpio_num=-1。
 * @param bus 来自 gs232b_esp_shared_spi_open，生命周期覆盖 MAC/PHY。
 * @param mac 成功返回新 MAC，由调用方安装/卸载驱动后以 del 释放。
 * @param phy 成功返回新 PHY，由调用方以 del 释放；不直接控制复位 GPIO。
 * @note 创建不探测芯片；驱动安装必须放在复位协调器恢复等待后的 resume 阶段。
 *       运行时恢复需停用并重建驱动；此工厂不配置 netif、MAC 地址或会话。
 */
gs232b_error_t gs232b_esp_w5500_create(gs232b_shared_spi_t *bus,
                                       esp_eth_mac_t **mac, esp_eth_phy_t **phy);
#endif
#endif
