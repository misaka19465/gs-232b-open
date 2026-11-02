#include "esp_app_internal.h"

static gs232b_time_us_t now(void *ctx)
{
    (void)ctx;
    return (gs232b_time_us_t)esp_timer_get_time();
}

static gs232b_error_t outputs_open(void *ctx, gs232b_shared_spi_t **bus)
{
    gs232b_esp_app_t *runtime = ctx;
    const gs232b_error_t error = gs232b_esp_shared_spi_open(&runtime->spi,
        GS232B_BOARD_SPI_CLOCK_HZ, GS232B_BOARD_SPI_TIMEOUT_MS);
    *bus = &runtime->spi.bus;
    return error;
}

static gs232b_error_t store_open(void *ctx, gs232b_store_t *store)
{
    gs232b_esp_app_t *runtime = ctx;
    const gs232b_error_t error = gs232b_esp_store_worker_open(&runtime->storage,
        &runtime->app.motion, &runtime->app.outputs, &runtime->app.dispatcher.control);
    *store = gs232b_esp_store_worker_port(&runtime->storage);
    return error;
}

static gs232b_error_t uart_open(void *ctx, gs232b_store_t store)
{
    return gs232b_esp_uart_open(&((gs232b_esp_app_t *)ctx)->uart, store);
}

static gs232b_error_t uart_boot(void *ctx)
{
    return gs232b_esp_uart_boot_service(&((gs232b_esp_app_t *)ctx)->uart);
}

static gs232b_error_t uart_attach(void *ctx, gs232b_dispatcher_t *dispatcher)
{
    gs232b_esp_app_t *runtime = ctx;
    return gs232b_esp_uart_attach(&runtime->uart, dispatcher,
        runtime->uart_line, sizeof(runtime->uart_line));
}

static gs232b_error_t uart_service(void *ctx)
{
    gs232b_esp_app_t *runtime = ctx;
    if (!gs232b_session_key_valid(runtime->uart.session)) {
        /* NOTE: TX/输入故障撤销旧会话后可重新 attach；匹配与波特率保存不会重做。 */
        return uart_attach(ctx, &runtime->app.dispatcher);
    }
    return gs232b_esp_uart_service(&runtime->uart);
}

static gs232b_session_io_t route(gs232b_esp_app_t *runtime, gs232b_session_key_t session)
{
    return runtime->uart.opening || (gs232b_session_key_valid(runtime->uart.session) &&
        gs232b_session_key_equal(runtime->uart.session, session)) ?
        gs232b_esp_uart_io(&runtime->uart) : gs232b_esp_ws_io(&runtime->ws);
}

static gs232b_error_t send_session(void *ctx, gs232b_session_key_t session, const uint8_t *bytes, size_t size)
{
    const gs232b_session_io_t io = route(ctx, session);
    return gs232b_session_send(&io, session, bytes, size);
}

static gs232b_error_t close_session(void *ctx, gs232b_session_key_t session)
{
    const gs232b_session_io_t io = route(ctx, session);
    return gs232b_session_close(&io, session);
}

gs232b_app_platform_t gs232b_esp_app_platform(gs232b_esp_app_t *runtime)
{
    return (gs232b_app_platform_t){.ctx = runtime, .clock = {NULL, now},
        .io = {.ctx = runtime, .send = send_session, .close = close_session},
        .outputs_open = outputs_open, .store_open = store_open, .adc_open = gs232b_app_adc_open,
        .uart_open = uart_open, .uart_boot = uart_boot, .uart_attach = uart_attach,
        .uart_service = uart_service, .network_bind = gs232b_app_network_bind,
        .network_start = gs232b_app_network_start, .network_service = gs232b_app_network_service};
}
