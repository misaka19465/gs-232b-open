#ifndef TEST_UART_API_H
#define TEST_UART_API_H
#include "driver/uart.h"
#include "freertos/task.h"
typedef struct {
    uart_config_t config;
    QueueHandle_t events;
    int rx_size, tx_size, event_count;
    int tx_pin, rx_pin, rts_pin, cts_pin;
    bool installed, fail_task, fail_delete, fail_flush, fail_read, fail_inspect, fail_tx;
    bool tx_busy, defer_exit;
    int fail_init_at, init_calls, fail_alloc_at, alloc_calls;
    unsigned live_queues, live_semaphores, live_tasks, rx_reads, tx_calls, flushes;
    unsigned ticks_waited, task_priority, task_stack;
    TaskFunction_t task_entry;
    void *task_ctx;
    size_t tx_limit, wire_size, rx_count;
    uint8_t wire[65536];
    uint8_t rx[32768];
} fake_uart_t;
extern fake_uart_t fake_uart;
void fake_uart_reset(void);
void fake_uart_input(const void *bytes, size_t size);
void fake_uart_event(uart_event_type_t type);
bool fake_uart_allocation(void);
#endif
