#ifndef GS232B_CONCURRENCY_TEST_SUPPORT_H
#define GS232B_CONCURRENCY_TEST_SUPPORT_H

#include "gs232b/esp_uart.h"
#include "esp_uart_internal.h"
#include "uart_api.h"
#include "ws_test_support.h"
#include "gs232b/spi_bus.h"

typedef struct {
    ws_fixture_t network;
    gs232b_esp_uart_t uart;
    gs232b_fake_spi_bus_t spi;
    gs232b_shared_spi_t bus;
    bool spi_open;
    uint8_t line[GS232B_LINE_STORAGE_BYTES];
} concurrency_fixture_t;

bool concurrency_init(concurrency_fixture_t *f);
bool concurrency_close(concurrency_fixture_t *f);
bool concurrency_poll(concurrency_fixture_t *f, size_t count);
bool concurrency_bounds(concurrency_fixture_t *f);
void concurrency_time(concurrency_fixture_t *f, gs232b_time_us_t now);
bool concurrency_net_expect(int fd, const char *expected);
bool concurrency_uart_expect(const char *expected);
bool concurrency_uart_input(concurrency_fixture_t *f, const char *input);
bool concurrency_net_command(concurrency_fixture_t *f, int fd, const char *input,
    const char *expected);
bool concurrency_enable_spi(concurrency_fixture_t *f);
bool concurrency_upload_tests(void);
bool concurrency_pressure_tests(void);
bool concurrency_fault_tests(void);

#endif
