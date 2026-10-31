#include "esp_uart_internal.h"

#include <string.h>

gs232b_error_t gs232b_uart_error(esp_err_t error)
{
    switch (error) {
    case ESP_OK: return GS232B_OK;
    case ESP_ERR_INVALID_ARG: return GS232B_ERROR_INVALID_ARGUMENT;
    case ESP_ERR_NO_MEM: return GS232B_ERROR_RESOURCE_EXHAUSTED;
    case ESP_ERR_INVALID_STATE: return GS232B_ERROR_NOT_READY;
    case ESP_ERR_TIMEOUT: return GS232B_ERROR_TIMEOUT;
    default: return GS232B_ERROR_IO;
    }
}

static gs232b_error_t send_bytes(void *ctx, gs232b_session_key_t session,
                                 const uint8_t *bytes, size_t size)
{
    gs232b_esp_uart_t *adapter = ctx;
    if (adapter == NULL || !gs232b_session_key_valid(session) ||
        (bytes == NULL && size != 0)) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened) { return GS232B_ERROR_NOT_READY; }
    /* NOTE: dispatcher 在 open 返回前发送欢迎语；仅允许 attach 的同步开会话窗口绑定。 */
    if (adapter->opening && !gs232b_session_key_valid(adapter->session)) {
        adapter->session = session;
    }
    if (!gs232b_session_key_equal(session, adapter->session)) {
        return GS232B_ERROR_STALE_SESSION;
    }
    if (size > GS232B_UART_TX_BYTES - adapter->tx_count) {
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }
    for (size_t i = 0; i < size; ++i) {
        adapter->tx[(adapter->tx_head + adapter->tx_count + i) % GS232B_UART_TX_BYTES] = bytes[i];
    }
    adapter->tx_count += size;
    return GS232B_OK;
}

static gs232b_error_t close_session(void *ctx, gs232b_session_key_t session)
{
    gs232b_esp_uart_t *adapter = ctx;
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!gs232b_session_key_valid(session) ||
        !gs232b_session_key_equal(session, adapter->session)) {
        return GS232B_ERROR_STALE_SESSION;
    }
    atomic_store(&adapter->enabled, false);
    adapter->session = (gs232b_session_key_t){0};
    adapter->tx_head = adapter->tx_count = 0;
    return GS232B_OK;
}

gs232b_session_io_t gs232b_esp_uart_io(gs232b_esp_uart_t *adapter)
{
    return (gs232b_session_io_t){.ctx = adapter, .send = send_bytes, .close = close_session};
}

gs232b_error_t gs232b_esp_uart_open(gs232b_esp_uart_t *adapter, uint32_t baud)
{
    if (adapter == NULL || adapter->opened || adapter->driver_installed ||
        adapter->rx_lock != NULL || adapter->stopped != NULL || adapter->packets != NULL ||
        adapter->rx_task != NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (baud == 0) { baud = GS232B_UART_DEFAULT_BAUD; }
    if (baud != 1200 && baud != 2400 && baud != 4800 && baud != 9600) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (uart_is_driver_installed(UART_NUM_1)) { return GS232B_ERROR_BUSY; }
    atomic_init(&adapter->enabled, false);
    atomic_init(&adapter->stopping, false);
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
        .baud_rate = (int)baud, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT
    };
    error = uart_param_config(UART_NUM_1, &config);
    if (error == ESP_OK) {
        error = uart_set_pin(UART_NUM_1, 6, 7, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (error != ESP_OK) { result = gs232b_uart_error(error); goto fail; }
    adapter->opened = true;
    if (xTaskCreate(gs232b_uart_rx_task, "gs_uart_rx", 3072, adapter, 8,
                    &adapter->rx_task) != pdPASS) { goto fail; }
    return GS232B_OK;
fail:
    (void)gs232b_esp_uart_close(adapter);
    return result;
}

gs232b_error_t gs232b_esp_uart_attach(gs232b_esp_uart_t *adapter,
    gs232b_dispatcher_t *dispatcher, uint8_t *line_storage, size_t capacity)
{
    if (adapter == NULL || !adapter->opened || atomic_load(&adapter->stopping) ||
        dispatcher == NULL || !dispatcher->initialized ||
        gs232b_session_key_valid(adapter->session)) { return GS232B_ERROR_INVALID_ARGUMENT; }
    /* NOTE: 前一会话已交给物理 FIFO 的字节不能撤回；排空前禁止重用通道。 */
    if (uart_wait_tx_done(UART_NUM_1, 0) != ESP_OK) { return GS232B_ERROR_WOULD_BLOCK; }
    adapter->dispatcher = dispatcher;
    adapter->opening = true;
    gs232b_session_key_t session;
    gs232b_error_t result = gs232b_dispatcher_session_open(dispatcher,
        GS232B_TRANSPORT_UART1, line_storage, capacity, &session);
    adapter->opening = false;
    if (result != GS232B_OK) { return result; }
    if (!gs232b_session_key_equal(adapter->session, session)) {
        (void)gs232b_dispatcher_session_close(dispatcher, session);
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    (void)xSemaphoreTake(adapter->rx_lock, portMAX_DELAY);
    result = gs232b_dispatcher_capture(dispatcher, session, &adapter->rx_context);
    if (result == GS232B_OK) { result = gs232b_uart_error(uart_flush_input(UART_NUM_1)); }
    (void)xQueueReset(adapter->packets);
    (void)xQueueReset(adapter->events);
    adapter->loss_pending = false;
    adapter->loss_reported = false;
    atomic_store(&adapter->enabled, result == GS232B_OK);
    (void)xSemaphoreGive(adapter->rx_lock);
    if (result != GS232B_OK) { (void)gs232b_dispatcher_session_close(dispatcher, session); }
    return result;
}

static gs232b_error_t transmit(gs232b_esp_uart_t *adapter)
{
    size_t count = GS232B_UART_TX_BYTES - adapter->tx_head;
    if (count > adapter->tx_count) { count = adapter->tx_count; }
    if (count > 128U) { count = 128U; }
    if (count == 0) { return GS232B_OK; }
    /* NOTE: TX driver ring=0 时 tx_chars 只填当前空闲硬件 FIFO，永不等线上发送。 */
    const int sent = uart_tx_chars(UART_NUM_1, (const char *)adapter->tx + adapter->tx_head,
                                   (uint32_t)count);
    if (sent < 0 || (size_t)sent > count) {
        (void)gs232b_dispatcher_session_close(adapter->dispatcher, adapter->session);
        return GS232B_ERROR_IO;
    }
    adapter->tx_head = (adapter->tx_head + (size_t)sent) % GS232B_UART_TX_BYTES;
    adapter->tx_count -= (size_t)sent;
    return GS232B_OK;
}

gs232b_error_t gs232b_esp_uart_service(gs232b_esp_uart_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened || !atomic_load(&adapter->enabled)) { return GS232B_ERROR_NOT_READY; }
    gs232b_command_context_t context;
    gs232b_error_t result = gs232b_dispatcher_capture(adapter->dispatcher,
        adapter->session, &context);
    if (result != GS232B_OK) {
        if (result == GS232B_ERROR_STALE_SESSION) {
            (void)gs232b_dispatcher_session_close(adapter->dispatcher, adapter->session);
        }
        return result;
    }
    if (xSemaphoreTake(adapter->rx_lock, 0) != pdPASS) { return GS232B_ERROR_WOULD_BLOCK; }
    gs232b_uart_events_locked(adapter);
    bool lost = adapter->loss_pending;
    bool report_loss = lost && !adapter->loss_reported;
    bool resync = false;
    gs232b_uart_packet_t packet = {0};
    if (!lost && context.epoch != adapter->rx_context.epoch) {
        size_t buffered = 0;
        const esp_err_t inspect = uart_get_buffered_data_len(UART_NUM_1, &buffered);
        resync = buffered != 0 || uxQueueMessagesWaiting(adapter->packets) != 0;
        const esp_err_t flush = uart_flush_input(UART_NUM_1);
        (void)xQueueReset(adapter->packets);
        adapter->rx_context = context;
        if (inspect != ESP_OK || flush != ESP_OK) {
            gs232b_uart_loss_locked(adapter);
            lost = report_loss = true;
        }
    } else if (!lost) {
        (void)xQueueReceive(adapter->packets, &packet, 0);
    }
    if (report_loss) { adapter->loss_reported = true; }
    (void)xSemaphoreGive(adapter->rx_lock);
    if (report_loss) {
        result = gs232b_dispatcher_input_lost(adapter->dispatcher, adapter->session);
    } else if (resync) {
        result = gs232b_dispatcher_resync_input(adapter->dispatcher, adapter->session);
    } else if (!lost && packet.size != 0) {
        result = gs232b_dispatcher_feed(adapter->dispatcher, packet.context, packet.bytes, packet.size);
    }
    if (!gs232b_session_key_valid(adapter->session)) { return result; }
    if (lost) {
        /* NOTE: 先完成控制者停机再发布新 epoch；清理期间 RX 不得盖新章重放旧字节。
         * 清理持续失败时同一故障只报告一次，并保持输入隔离。 */
        gs232b_command_context_t next;
        if (gs232b_dispatcher_capture(adapter->dispatcher, adapter->session, &next) == GS232B_OK &&
            xSemaphoreTake(adapter->rx_lock, 0) == pdPASS) {
            if (uart_flush_input(UART_NUM_1) == ESP_OK) {
                (void)xQueueReset(adapter->packets);
                adapter->rx_context = next;
                adapter->loss_pending = adapter->loss_reported = false;
            } else if (result == GS232B_OK) {
                result = GS232B_ERROR_IO;
            }
            (void)xSemaphoreGive(adapter->rx_lock);
        }
    }
    const gs232b_error_t sent = transmit(adapter);
    return result != GS232B_OK ? result : sent;
}

gs232b_error_t gs232b_esp_uart_close(gs232b_esp_uart_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    gs232b_error_t result = GS232B_OK;
    if (gs232b_session_key_valid(adapter->session) && adapter->dispatcher != NULL) {
        result = gs232b_dispatcher_session_close(adapter->dispatcher, adapter->session);
    }
    atomic_store(&adapter->enabled, false);
    atomic_store(&adapter->stopping, true);
    if (adapter->rx_task != NULL) {
        if (xSemaphoreTake(adapter->stopped, pdMS_TO_TICKS(100)) != pdPASS) {
            return GS232B_ERROR_TIMEOUT;
        }
        adapter->rx_task = NULL;
    }
    adapter->opened = false;
    adapter->session = (gs232b_session_key_t){0};
    adapter->opening = false;
    if (adapter->driver_installed) {
        const esp_err_t error = uart_driver_delete(UART_NUM_1);
        if (error != ESP_OK) { return gs232b_uart_error(error); }
        adapter->driver_installed = false;
        adapter->events = NULL;
    }
    if (adapter->packets != NULL) { vQueueDelete(adapter->packets); adapter->packets = NULL; }
    if (adapter->stopped != NULL) { vSemaphoreDelete(adapter->stopped); adapter->stopped = NULL; }
    if (adapter->rx_lock != NULL) { vSemaphoreDelete(adapter->rx_lock); adapter->rx_lock = NULL; }
    adapter->dispatcher = NULL;
    adapter->tx_head = adapter->tx_count = 0;
    return result;
}
