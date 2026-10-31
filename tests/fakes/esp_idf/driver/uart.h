#ifndef TEST_DRIVER_UART_H
#define TEST_DRIVER_UART_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/queue.h"
typedef enum { UART_NUM_0, UART_NUM_1 } uart_port_t;
enum { UART_DATA_8_BITS = 3, UART_PARITY_DISABLE = 0, UART_STOP_BITS_1 = 1,
       UART_HW_FLOWCTRL_DISABLE = 0, UART_SCLK_DEFAULT = 1, UART_PIN_NO_CHANGE = -1 };
typedef struct {
    int baud_rate;
    int data_bits;
    int parity;
    int stop_bits;
    int flow_ctrl;
    uint8_t rx_flow_ctrl_thresh;
    int source_clk;
} uart_config_t;
typedef enum {
    UART_DATA, UART_BREAK, UART_BUFFER_FULL, UART_FIFO_OVF, UART_FRAME_ERR,
    UART_PARITY_ERR, UART_DATA_BREAK, UART_PATTERN_DET
} uart_event_type_t;
typedef struct { uart_event_type_t type; size_t size; bool timeout_flag; } uart_event_t;
esp_err_t uart_driver_install(uart_port_t port, int rx, int tx, int events,
    QueueHandle_t *queue, int flags);
esp_err_t uart_driver_delete(uart_port_t port);
bool uart_is_driver_installed(uart_port_t port);
esp_err_t uart_param_config(uart_port_t port, const uart_config_t *config);
esp_err_t uart_set_baudrate(uart_port_t port, uint32_t baud);
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts);
int uart_read_bytes(uart_port_t port, void *buffer, uint32_t size, TickType_t ticks);
int uart_tx_chars(uart_port_t port, const char *buffer, uint32_t size);
esp_err_t uart_wait_tx_done(uart_port_t port, TickType_t ticks);
esp_err_t uart_flush_input(uart_port_t port);
esp_err_t uart_get_buffered_data_len(uart_port_t port, size_t *size);
#endif
