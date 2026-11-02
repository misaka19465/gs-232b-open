#ifndef GS232B_APP_TEST_SUPPORT_H
#define GS232B_APP_TEST_SUPPORT_H

#include "gs232b/app.h"
#include "gs232b/fakes.h"
#include "gs232b/spi_bus.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return false; } } while (0)

typedef struct {
    gs232b_app_t app;
    gs232b_fake_spi_bus_t spi;
    gs232b_shared_spi_t bus;
    gs232b_fake_store_t store;
    gs232b_fake_sessions_t sessions;
    gs232b_session_key_t uart;
    uint8_t line[GS232B_LINE_STORAGE_BYTES];
    gs232b_error_t sample_error, adc_error, boot_error, network_error, store_error;
    unsigned store_opens, adc_opens, uart_opens, attaches, uart_services, network_starts;
    bool safe_order;
} app_fixture_t;

static inline void advance(app_fixture_t *f, gs232b_time_us_t duration)
{
    atomic_fetch_add(&f->spi.now_us, duration);
}

static inline gs232b_error_t outputs_open(void *ctx, gs232b_shared_spi_t **bus)
{
    *bus = &((app_fixture_t *)ctx)->bus;
    return GS232B_OK;
}

static inline gs232b_error_t store_open(void *ctx, gs232b_store_t *store)
{
    app_fixture_t *f = ctx;
    ++f->store_opens;
    f->safe_order &= f->spi.latched == 0x10 && f->spi.pwm == 0 &&
        f->app.startup_reset.phase == GS232B_W5500_READY;
    *store = gs232b_fake_store_port(&f->store);
    return f->store_error;
}

static inline gs232b_error_t sample(void *ctx, gs232b_axis_id_t axis, gs232b_sample_t *value)
{
    (void)axis;
    app_fixture_t *f = ctx;
    *value = (gs232b_sample_t){.valid = f->sample_error == GS232B_OK,
        .acquired_at_us = gs232b_fake_spi_bus_now(&f->spi), .adc_uv = 500000, .raw = 1000};
    return f->sample_error;
}

static inline gs232b_error_t adc_open(void *ctx, gs232b_sampler_t *sampler)
{
    app_fixture_t *f = ctx;
    ++f->adc_opens;
    f->safe_order &= f->store_opens == 1;
    *sampler = (gs232b_sampler_t){f, sample};
    return f->adc_error;
}

static inline gs232b_error_t uart_open(void *ctx, gs232b_store_t store)
{
    (void)store;
    app_fixture_t *f = ctx;
    ++f->uart_opens;
    f->safe_order &= f->app.dispatcher.initialized;
    return GS232B_OK;
}

static inline gs232b_error_t uart_boot(void *ctx)
{
    return ((app_fixture_t *)ctx)->boot_error;
}

static inline gs232b_error_t uart_attach(void *ctx, gs232b_dispatcher_t *dispatcher)
{
    app_fixture_t *f = ctx;
    ++f->attaches;
    gs232b_session_key_t io;
    gs232b_error_t error = gs232b_fake_sessions_open(&f->sessions, GS232B_TRANSPORT_UART1, &io);
    if (error == GS232B_OK) error = gs232b_dispatcher_session_open(dispatcher,
        GS232B_TRANSPORT_UART1, f->line, sizeof(f->line), &f->uart);
    if (!gs232b_session_key_equal(f->uart, io)) return GS232B_ERROR_IO;
    return error;
}

static inline gs232b_error_t uart_service(void *ctx)
{
    ++((app_fixture_t *)ctx)->uart_services;
    return GS232B_OK;
}

static inline gs232b_error_t network_bind(void *ctx, gs232b_app_t *app)
{
    (void)ctx;
    (void)app;
    return GS232B_OK;
}

static inline gs232b_error_t network_start(void *ctx, gs232b_app_t *app)
{
    (void)app;
    app_fixture_t *f = ctx;
    ++f->network_starts;
    f->safe_order &= f->attaches == 1;
    return f->network_error;
}

static inline gs232b_error_t network_service(void *ctx, gs232b_app_t *app)
{
    (void)app;
    return ((app_fixture_t *)ctx)->network_error;
}

static inline bool fixture_init(app_fixture_t *f, bool zero)
{
    memset(f, 0, sizeof(*f));
    CHECK(gs232b_fake_spi_bus_init(&f->spi, &f->bus));
    f->spi.latched = 0x0f;
    f->spi.pwm = 1000;
    f->safe_order = true;
    gs232b_fake_store_init(&f->store);
    CHECK(gs232b_fake_sessions_init(&f->sessions,
        (gs232b_clock_t){&f->spi, gs232b_fake_spi_bus_now}) == GS232B_OK);
    if (zero) {
        const gs232b_store_t store = gs232b_fake_store_port(&f->store);
        const gs232b_feedback_reading_t reading = {.valid = true, .adc_uv = 100000};
        for (int i = 0; i < GS232B_AXIS_COUNT; ++i)
            CHECK(gs232b_feedback_zero_save(&store, (gs232b_axis_id_t)i, &reading, 0) == GS232B_OK);
    }
    return true;
}

static inline bool start(app_fixture_t *f, bool verified)
{
    gs232b_motion_config_t config;
    gs232b_time_us_t reversal;
    gs232b_app_board_config(&config, &reversal);
    config.parameters_verified = verified;
    const gs232b_app_platform_t platform = {.ctx = f,
        .clock = {&f->spi, gs232b_fake_spi_bus_now}, .io = gs232b_fake_sessions_port(&f->sessions),
        .outputs_open = outputs_open, .store_open = store_open, .adc_open = adc_open,
        .uart_open = uart_open, .uart_boot = uart_boot, .uart_attach = uart_attach,
        .uart_service = uart_service, .network_bind = network_bind,
        .network_start = network_start, .network_service = network_service};
    return gs232b_app_start(&f->app, platform, &config, reversal) == GS232B_OK;
}

static inline bool boot(app_fixture_t *f, bool verified)
{
    CHECK(start(f, verified));
    advance(f, GS232B_W5500_RESET_DEFAULT_US);
    CHECK(gs232b_app_step(&f->app) == GS232B_OK);
    advance(f, GS232B_W5500_RECOVERY_DEFAULT_US);
    CHECK(gs232b_app_step(&f->app) == GS232B_OK);
    CHECK(gs232b_app_step(&f->app) == GS232B_OK);
    CHECK(f->app.status.phase == GS232B_APP_RUNNING);
    return true;
}

static inline bool command(app_fixture_t *f, const char *text)
{
    gs232b_command_context_t context;
    CHECK(gs232b_dispatcher_capture(&f->app.dispatcher, f->uart, &context) == GS232B_OK);
    CHECK(gs232b_dispatcher_feed(&f->app.dispatcher, context,
        (const uint8_t *)text, strlen(text)) == GS232B_OK);
    return true;
}

static inline bool expect(app_fixture_t *f, const char *part)
{
    uint8_t bytes[4097];
    size_t size;
    CHECK(gs232b_fake_sessions_take_tx(&f->sessions, f->uart, bytes, sizeof(bytes) - 1U, &size) == GS232B_OK);
    bytes[size] = 0;
    CHECK(strstr((char *)bytes, part) != NULL);
    return true;
}

#endif
