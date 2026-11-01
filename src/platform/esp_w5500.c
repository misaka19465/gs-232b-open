#include "gs232b/esp_w5500.h"

#include "driver/gpio.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"

static esp_err_t map_error(gs232b_error_t error)
{
    switch (error) {
    case GS232B_OK: return ESP_OK;
    case GS232B_ERROR_TIMEOUT: return ESP_ERR_TIMEOUT;
    case GS232B_ERROR_INVALID_ARGUMENT: return ESP_ERR_INVALID_ARG;
    case GS232B_ERROR_NOT_READY: return ESP_ERR_INVALID_STATE;
    default: return ESP_FAIL;
    }
}

static void *spi_init(const void *config)
{
    /* NOTE: bus 由板级适配器持有；MAC 不注册第三个 device，也不销毁总线。 */
    return (void *)config;
}

static esp_err_t spi_deinit(void *ctx)
{
    (void)ctx;
    return ESP_OK;
}

static esp_err_t spi_read(void *ctx, uint32_t cmd, uint32_t addr, void *data, uint32_t len)
{
    if (cmd > UINT16_MAX || addr > UINT8_MAX) { return ESP_ERR_INVALID_ARG; }
    return map_error(gs232b_shared_spi_w5500_transfer(
        ctx, (uint8_t)addr, (uint16_t)cmd, NULL, data, len));
}

static esp_err_t spi_write(void *ctx, uint32_t cmd, uint32_t addr,
                            const void *data, uint32_t len)
{
    if (cmd > UINT16_MAX || addr > UINT8_MAX) { return ESP_ERR_INVALID_ARG; }
    return map_error(gs232b_shared_spi_w5500_transfer(
        ctx, (uint8_t)addr, (uint16_t)cmd, data, NULL, len));
}

gs232b_error_t gs232b_esp_w5500_create(gs232b_shared_spi_t *bus,
                                       esp_eth_mac_t **mac, esp_eth_phy_t **phy)
{
    if (mac != NULL) { *mac = NULL; }
    if (phy != NULL) { *phy = NULL; }
    if (bus == NULL || mac == NULL || phy == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    /* NOTE: 官方 WIZnet 驱动只添加 GPIO10 handler，不安装共享 GPIO ISR 服务。 */
    const esp_err_t isr = gpio_install_isr_service(0);
    if (isr != ESP_OK && isr != ESP_ERR_INVALID_STATE) {
        return isr == ESP_ERR_NO_MEM ? GS232B_ERROR_RESOURCE_EXHAUSTED : GS232B_ERROR_IO;
    }
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_w5500_config_t chip = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, NULL);
    chip.base.int_gpio_num = 10;
    chip.base.custom_spi_driver = (eth_spi_custom_driver_config_t){
        .config = bus, .init = spi_init, .deinit = spi_deinit,
        .read = spi_read, .write = spi_write
    };
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = 1;
    phy_config.reset_gpio_num = -1;
    *mac = esp_eth_mac_new_w5500(&chip, &mac_config);
    if (*mac == NULL) { return GS232B_ERROR_RESOURCE_EXHAUSTED; }
    *phy = esp_eth_phy_new_w5500(&phy_config);
    if (*phy == NULL) {
        (*mac)->del(*mac);
        *mac = NULL;
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    return GS232B_OK;
}
