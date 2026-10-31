#include "gs232b/esp_shared_spi.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_rom_sys.h"

#define SPI_MOSI GPIO_NUM_4
#define SPI_MISO GPIO_NUM_5
#define SPI_SCLK GPIO_NUM_3
#define W5500_CS GPIO_NUM_8
#define HC595_LATCH GPIO_NUM_2
#define SPEED_PWM GPIO_NUM_20
#define PWM_MAX 1023U

static gs232b_error_t map_error(esp_err_t error)
{
    switch (error) {
    case ESP_OK: return GS232B_OK;
    case ESP_ERR_TIMEOUT: return GS232B_ERROR_TIMEOUT;
    case ESP_ERR_NO_MEM: return GS232B_ERROR_RESOURCE_EXHAUSTED;
    case ESP_ERR_INVALID_ARG: return GS232B_ERROR_INVALID_ARGUMENT;
    default: return GS232B_ERROR_IO;
    }
}

static gs232b_error_t lock(void *ctx)
{
    gs232b_esp_shared_spi_t *adapter = ctx;
    return xSemaphoreTake(adapter->mutex, adapter->timeout_ticks) == pdTRUE ?
           GS232B_OK : GS232B_ERROR_TIMEOUT;
}

static void unlock(void *ctx)
{
    gs232b_esp_shared_spi_t *adapter = ctx;
    xSemaphoreGive(adapter->mutex);
}

static gs232b_error_t acquire(void *ctx)
{
    gs232b_esp_shared_spi_t *adapter = ctx;
    /* NOTE: IDF 6.1 仅支持 portMAX_DELAY。所有 device 访问均已由有限入口锁
     * 串行化且无异步队列，此处没有其他事务等待；不得绕过本适配器。 */
    return map_error(spi_device_acquire_bus(adapter->shift, portMAX_DELAY));
}

static void release(void *ctx)
{
    gs232b_esp_shared_spi_t *adapter = ctx;
    spi_device_release_bus(adapter->shift);
}

static gs232b_error_t transmit_595(void *ctx, uint8_t byte)
{
    gs232b_esp_shared_spi_t *adapter = ctx;
    spi_transaction_t transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {byte}
    };
    /* NOTE: 同步调用含传输完成等待；返回前不锁存、不释放 host。 */
    return map_error(spi_device_polling_transmit(adapter->shift, &transaction));
}

static gs232b_error_t set_latch(void *ctx, bool high)
{
    (void)ctx;
    const esp_err_t error = gpio_set_level(HC595_LATCH, high ? 1 : 0);
    /* NOTE: 数据建立/保持及 STCP 高低各至少 1us，实际边沿仍需示波器验收。 */
    esp_rom_delay_us(1);
    return map_error(error);
}

static gs232b_error_t transmit_w5500(void *ctx, uint8_t control, uint16_t address,
                                    const void *tx, void *rx, size_t size)
{
    gs232b_esp_shared_spi_t *adapter = ctx;
    spi_transaction_t transaction = {
        .cmd = address,
        .addr = control,
        .length = size * 8U,
        .tx_buffer = tx,
        .rx_buffer = rx
    };
    if (rx != NULL && size <= sizeof(transaction.rx_data)) {
        /* NOTE: DMA 小寄存器读走内联缓冲区，避免覆盖调用方相邻字节。 */
        transaction.flags = SPI_TRANS_USE_RXDATA;
    }
    const esp_err_t error = spi_device_polling_transmit(adapter->ethernet, &transaction);
    if (error == ESP_OK && (transaction.flags & SPI_TRANS_USE_RXDATA) != 0) {
        memcpy(rx, transaction.rx_data, size);
    }
    return map_error(error);
}

static gs232b_error_t set_pwm(void *ctx, uint16_t permille)
{
    (void)ctx;
    /* NOTE: 满量程写 1023 而非 1024，避免 C3 的 10bit 计数越界。 */
    const uint32_t duty = ((uint32_t)permille * PWM_MAX + 500U) / 1000U;
    esp_err_t error = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (error == ESP_OK) {
        error = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    return map_error(error);
}

gs232b_error_t gs232b_esp_shared_spi_close(gs232b_esp_shared_spi_t *adapter)
{
    if (adapter == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    esp_err_t error;
    if (adapter->shift != NULL) {
        error = spi_bus_remove_device(adapter->shift);
        if (error != ESP_OK) { return map_error(error); }
        adapter->shift = NULL;
    }
    if (adapter->ethernet != NULL) {
        error = spi_bus_remove_device(adapter->ethernet);
        if (error != ESP_OK) { return map_error(error); }
        adapter->ethernet = NULL;
    }
    if (adapter->host_open) {
        error = spi_bus_free(SPI2_HOST);
        if (error != ESP_OK) { return map_error(error); }
        adapter->host_open = false;
    }
    if (adapter->pwm_open) {
        error = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        if (error != ESP_OK) { return map_error(error); }
        adapter->pwm_open = false;
    }
    if (adapter->mutex != NULL) {
        vSemaphoreDelete(adapter->mutex);
        adapter->mutex = NULL;
    }
    memset(&adapter->bus, 0, sizeof(adapter->bus));
    return GS232B_OK;
}

gs232b_error_t gs232b_esp_shared_spi_open(gs232b_esp_shared_spi_t *adapter,
                                          int clock_hz, uint32_t timeout_ms)
{
    if (adapter == NULL || clock_hz <= 0 || clock_hz > GS232B_BOARD_SPI_CLOCK_HZ ||
        timeout_ms == 0 || timeout_ms > 1000U || adapter->mutex != NULL ||
        adapter->host_open || adapter->pwm_open) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    adapter->timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if (adapter->timeout_ticks == 0) { adapter->timeout_ticks = 1; }
    adapter->mutex = xSemaphoreCreateMutex();
    if (adapter->mutex == NULL) { return GS232B_ERROR_RESOURCE_EXHAUSTED; }

    /* NOTE: 在启用 GPIO 输出前预置 STCP=0、nSS=1；不把 GPIO2 配成 reset。 */
    esp_err_t error = gpio_set_level(HC595_LATCH, 0);
    if (error == ESP_OK) { error = gpio_set_level(W5500_CS, 1); }
    const gpio_config_t pins = {
        .pin_bit_mask = (UINT64_C(1) << HC595_LATCH) | (UINT64_C(1) << W5500_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    if (error == ESP_OK) { error = gpio_config(&pins); }
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    if (error == ESP_OK) { error = ledc_timer_config(&timer); }
    const ledc_channel_config_t pwm = {
        .gpio_num = SPEED_PWM, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0, .duty = 0, .hpoint = 0
    };
    if (error == ESP_OK) {
        error = ledc_channel_config(&pwm);
        adapter->pwm_open = error == ESP_OK;
    }
    const spi_bus_config_t host = {
        .mosi_io_num = SPI_MOSI, .miso_io_num = SPI_MISO, .sclk_io_num = SPI_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = GS232B_SHARED_SPI_MAX_TRANSFER
    };
    if (error == ESP_OK) {
        error = spi_bus_initialize(SPI2_HOST, &host, SPI_DMA_CH_AUTO);
        adapter->host_open = error == ESP_OK;
    }
    const spi_device_interface_config_t shift = {
        .clock_speed_hz = clock_hz, .mode = 0, .spics_io_num = -1, .queue_size = 1
    };
    if (error == ESP_OK) { error = spi_bus_add_device(SPI2_HOST, &shift, &adapter->shift); }
    const spi_device_interface_config_t ethernet = {
        .command_bits = 16, .address_bits = 8, .clock_speed_hz = clock_hz,
        .mode = 0, .spics_io_num = W5500_CS, .queue_size = 1
    };
    if (error == ESP_OK) {
        error = spi_bus_add_device(SPI2_HOST, &ethernet, &adapter->ethernet);
    }
    if (error != ESP_OK) {
        (void)gs232b_esp_shared_spi_close(adapter);
        return map_error(error);
    }
    return gs232b_shared_spi_init(&adapter->bus, (gs232b_shared_spi_io_t){
        .ctx = adapter, .lock = lock, .unlock = unlock, .acquire_595 = acquire,
        .transmit_595 = transmit_595, .set_latch = set_latch, .release_595 = release,
        .transmit_w5500 = transmit_w5500, .set_pwm = set_pwm
    });
}
