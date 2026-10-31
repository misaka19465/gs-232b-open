#include "esp_uart_internal.h"

#include "esp_timer.h"

gs232b_error_t gs232b_esp_uart_open(gs232b_esp_uart_t *adapter, gs232b_store_t store)
{
    if (adapter == NULL || adapter->opened || adapter->driver_installed ||
        adapter->rx_lock != NULL || adapter->stopped != NULL || adapter->packets != NULL ||
        adapter->rx_task != NULL || adapter->capture.channel != NULL ||
        adapter->capture.completions != NULL ||
        store.load == NULL || store.save == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (uart_is_driver_installed(UART_NUM_1)) { return GS232B_ERROR_BUSY; }
    atomic_init(&adapter->enabled, false);
    atomic_init(&adapter->stopping, false);
    adapter->baud_store = store;
    adapter->boot = (gs232b_uart_boot_status_t){0};
    adapter->boot.load_error = gs232b_uart_baud_load(&store,
        &adapter->boot.saved_baud, &adapter->baud_revision);
    adapter->boot.baud = adapter->boot.saved_baud;
    adapter->baud_selected = adapter->baud_applied = adapter->baud_save_attempted = false;
    adapter->rx_lock = xSemaphoreCreateMutex();
    adapter->stopped = xSemaphoreCreateBinary();
    adapter->packets = xQueueCreate(GS232B_UART_RX_PACKETS, sizeof(gs232b_uart_packet_t));
    gs232b_error_t result = GS232B_ERROR_RESOURCE_EXHAUSTED;
    if (adapter->rx_lock == NULL || adapter->stopped == NULL || adapter->packets == NULL) {
        goto fail;
    }
    esp_err_t error = uart_driver_install(UART_NUM_1, GS232B_UART_RX_RING_BYTES,
        0, GS232B_UART_EVENTS, &adapter->events, 0);
    if (error != ESP_OK) { result = gs232b_uart_error(error); goto fail; }
    adapter->driver_installed = true;
    const uart_config_t config = {
        .baud_rate = (int)adapter->boot.baud, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT
    };
    error = uart_param_config(UART_NUM_1, &config);
    if (error == ESP_OK) {
        error = uart_set_pin(UART_NUM_1, 6, 7, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (error != ESP_OK) { result = gs232b_uart_error(error); goto fail; }
    result = gs232b_uart_capture_open(&adapter->capture);
    if (result != GS232B_OK) { goto fail; }
    adapter->boot_started_us = esp_timer_get_time();
    adapter->opened = true;
    if (xTaskCreate(gs232b_uart_rx_task, "gs_uart_rx", 3072, adapter, 8,
                    &adapter->rx_task) != pdPASS) {
        result = GS232B_ERROR_RESOURCE_EXHAUSTED;
        goto fail;
    }
    return GS232B_OK;
fail:
    (void)gs232b_esp_uart_close(adapter);
    return result;
}

gs232b_uart_boot_status_t gs232b_esp_uart_boot_status(const gs232b_esp_uart_t *adapter)
{
    return adapter == NULL ? (gs232b_uart_boot_status_t){0} : adapter->boot;
}

gs232b_error_t gs232b_esp_uart_boot_service(gs232b_esp_uart_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened || atomic_load(&adapter->stopping)) { return GS232B_ERROR_NOT_READY; }
    if (adapter->boot.ready) { return GS232B_OK; }
    const int64_t deadline = adapter->boot_started_us + (int64_t)GS232B_UART_BAUD_WINDOW_US;
    if (!adapter->baud_selected) {
        gs232b_error_t result = gs232b_uart_capture_service(&adapter->capture, deadline);
        if (result != GS232B_OK) { return result; }
        const uint32_t detected = adapter->capture.detector.baud;
        if (detected == 0 && esp_timer_get_time() < deadline) {
            return GS232B_ERROR_WOULD_BLOCK;
        }
        adapter->boot.baud = detected == 0 ? adapter->boot.saved_baud : detected;
        adapter->boot.matched = detected != 0;
        adapter->baud_selected = true;
    }
    gs232b_error_t result = gs232b_uart_capture_close(&adapter->capture);
    if (result != GS232B_OK) { return result; }
    if (!adapter->baud_applied) {
        /* NOTE: 只在确认/超时后应用一个最终速率，不扫描 UART 的四种配置。 */
        result = gs232b_uart_error(uart_set_baudrate(UART_NUM_1, adapter->boot.baud));
        if (result != GS232B_OK) { return result; }
        adapter->baud_applied = true;
    }
    if (!adapter->baud_save_attempted) {
        adapter->baud_save_attempted = true;
        if (adapter->boot.matched &&
            (adapter->boot.baud != adapter->boot.saved_baud || adapter->baud_revision == 0)) {
            uint64_t revision = 0;
            adapter->boot.save_error = gs232b_uart_baud_commit(&adapter->baud_store,
                adapter->baud_revision, adapter->boot.baud, &revision);
            if (adapter->boot.save_error == GS232B_OK) {
                adapter->baud_revision = revision;
                adapter->boot.saved_baud = adapter->boot.baud;
            }
        }
    }
    /* NOTE: 错误速率解码的字节、匹配 CR 和旧事件均不能进入新会话。 */
    result = gs232b_uart_error(uart_flush_input(UART_NUM_1));
    if (result != GS232B_OK) { return result; }
    (void)xQueueReset(adapter->events);
    (void)xQueueReset(adapter->packets);
    adapter->boot.ready = true;
    return GS232B_OK;
}
