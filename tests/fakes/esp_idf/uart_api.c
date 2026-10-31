#include "uart_api.h"
#include <assert.h>
#include <string.h>

fake_uart_t fake_uart;

void fake_uart_reset(void)
{
    assert(fake_uart.live_queues == 0 && fake_uart.live_semaphores == 0 &&
           fake_uart.live_tasks == 0 && !fake_uart.installed);
    memset(&fake_uart, 0, sizeof(fake_uart));
    fake_uart.tx_limit = 128;
}

bool fake_uart_allocation(void)
{
    return ++fake_uart.alloc_calls != fake_uart.fail_alloc_at;
}

static esp_err_t init_result(void)
{
    return ++fake_uart.init_calls == fake_uart.fail_init_at ? ESP_FAIL : ESP_OK;
}

bool uart_is_driver_installed(uart_port_t port)
{
    assert(port == UART_NUM_1);
    return fake_uart.installed;
}

esp_err_t uart_driver_install(uart_port_t port, int rx, int tx, int events,
    QueueHandle_t *queue, int flags)
{
    assert(port == UART_NUM_1 && !fake_uart.installed && flags == 0);
    esp_err_t error = init_result();
    if (error != ESP_OK) { return error; }
    fake_uart.events = xQueueCreate((UBaseType_t)events, sizeof(uart_event_t));
    if (fake_uart.events == NULL) { return ESP_ERR_NO_MEM; }
    fake_uart.installed = true;
    fake_uart.rx_size = rx;
    fake_uart.tx_size = tx;
    fake_uart.event_count = events;
    *queue = fake_uart.events;
    return ESP_OK;
}

esp_err_t uart_driver_delete(uart_port_t port)
{
    assert(port == UART_NUM_1 && fake_uart.installed);
    if (fake_uart.fail_delete) { return ESP_FAIL; }
    vQueueDelete(fake_uart.events);
    fake_uart.events = NULL;
    fake_uart.installed = false;
    fake_uart.rx_count = 0;
    return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t port, const uart_config_t *config)
{
    assert(port == UART_NUM_1 && fake_uart.installed);
    fake_uart.config = *config;
    return init_result();
}

esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts)
{
    assert(port == UART_NUM_1 && fake_uart.installed);
    fake_uart.tx_pin = tx; fake_uart.rx_pin = rx;
    fake_uart.rts_pin = rts; fake_uart.cts_pin = cts;
    return init_result();
}

void fake_uart_event(uart_event_type_t type)
{
    const uart_event_t event = {.type = type};
    (void)xQueueSend(fake_uart.events, &event, 0);
}

void fake_uart_input(const void *bytes, size_t size)
{
    size_t room = sizeof(fake_uart.rx) - fake_uart.rx_count;
    size_t accepted = size < room ? size : room;
    memcpy(fake_uart.rx + fake_uart.rx_count, bytes, accepted);
    fake_uart.rx_count += accepted;
    fake_uart_event(size > room ? UART_FIFO_OVF : UART_DATA);
}

int uart_read_bytes(uart_port_t port, void *buffer, uint32_t size, TickType_t ticks)
{
    assert(port == UART_NUM_1 && fake_uart.installed && ticks == 0 && size <= 256);
    ++fake_uart.rx_reads;
    if (fake_uart.fail_read) { return -1; }
    size_t count = size < fake_uart.rx_count ? size : fake_uart.rx_count;
    memcpy(buffer, fake_uart.rx, count);
    memmove(fake_uart.rx, fake_uart.rx + count, fake_uart.rx_count - count);
    fake_uart.rx_count -= count;
    return (int)count;
}

int uart_tx_chars(uart_port_t port, const char *buffer, uint32_t size)
{
    assert(port == UART_NUM_1 && fake_uart.installed && fake_uart.tx_size == 0 && size <= 128);
    ++fake_uart.tx_calls;
    if (fake_uart.fail_tx) { return -1; }
    size_t count = size < fake_uart.tx_limit ? size : fake_uart.tx_limit;
    assert(fake_uart.wire_size + count <= sizeof(fake_uart.wire));
    memcpy(fake_uart.wire + fake_uart.wire_size, buffer, count);
    fake_uart.wire_size += count;
    return (int)count;
}

esp_err_t uart_wait_tx_done(uart_port_t port, TickType_t ticks)
{
    assert(port == UART_NUM_1 && ticks == 0);
    return fake_uart.tx_busy ? ESP_ERR_TIMEOUT : ESP_OK;
}

esp_err_t uart_flush_input(uart_port_t port)
{
    assert(port == UART_NUM_1 && fake_uart.installed);
    ++fake_uart.flushes;
    if (fake_uart.fail_flush) { return ESP_FAIL; }
    fake_uart.rx_count = 0;
    return ESP_OK;
}

esp_err_t uart_get_buffered_data_len(uart_port_t port, size_t *size)
{
    assert(port == UART_NUM_1 && fake_uart.installed);
    if (fake_uart.fail_inspect) { return ESP_FAIL; }
    *size = fake_uart.rx_count;
    return ESP_OK;
}
