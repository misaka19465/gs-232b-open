#include "gs232b/app.h"

#include <string.h>

static gs232b_error_t no_driver(void *ctx)
{
    (void)ctx;
    return GS232B_OK;
}

static gs232b_error_t missing_sample(void *ctx, gs232b_axis_id_t axis, gs232b_sample_t *sample)
{
    (void)ctx;
    (void)axis;
    *sample = (gs232b_sample_t){0};
    return GS232B_ERROR_NOT_READY;
}

static gs232b_error_t fail(gs232b_app_t *app, gs232b_error_t error)
{
    /* NOTE: 输出失败保留 unknown 证据；不自动重试、不启动外设或接纳运动。 */
    app->status.fatal_error = error;
    app->status.phase = GS232B_APP_FAILED;
    return error;
}

gs232b_error_t gs232b_app_start(gs232b_app_t *app, gs232b_app_platform_t platform,
    const gs232b_motion_config_t *config, gs232b_time_us_t reversal_us)
{
    if (app == NULL || config == NULL || reversal_us == 0 ||
        platform.clock.now_us == NULL || platform.io.send == NULL || platform.io.close == NULL ||
        platform.outputs_open == NULL || platform.store_open == NULL || platform.adc_open == NULL ||
        platform.uart_open == NULL || platform.uart_boot == NULL || platform.uart_attach == NULL ||
        platform.uart_service == NULL || platform.network_bind == NULL ||
        platform.network_start == NULL || platform.network_service == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(app, 0, sizeof(*app));
    app->platform = platform;
    app->config = *config;
    gs232b_error_t error = platform.outputs_open(platform.ctx, &app->bus);
    if (error != GS232B_OK) return fail(app, error);
    error = gs232b_output_manager_init(&app->outputs, platform.clock,
        gs232b_shared_spi_output_driver(app->bus), reversal_us);
    if (error != GS232B_OK) return fail(app, error);
    error = gs232b_w5500_reset_init(&app->startup_reset, &app->outputs, app->bus,
        (gs232b_w5500_lifecycle_t){NULL, no_driver, no_driver},
        GS232B_W5500_RESET_DEFAULT_US, GS232B_W5500_RECOVERY_DEFAULT_US);
    if (error == GS232B_OK) error = gs232b_w5500_reset_begin(&app->startup_reset);
    if (error != GS232B_OK) return fail(app, error);
    app->status.phase = GS232B_APP_RESET_WAIT;
    return GS232B_OK;
}

static gs232b_error_t initialize(gs232b_app_t *app)
{
    gs232b_app_platform_t *p = &app->platform;
    app->status.store_error = p->store_open(p->ctx, &app->store);
    if (app->status.store_error != GS232B_OK) app->config.parameters_verified = false;
    gs232b_sampler_t sampler = {0};
    app->status.adc_error = p->adc_open(p->ctx, &sampler);
    if (sampler.read == NULL) sampler = (gs232b_sampler_t){.read = missing_sample};
    for (int i = 0; i < GS232B_AXIS_COUNT; ++i) {
        uint32_t zero = 0;
        app->status.zero_error[i] = gs232b_feedback_zero_load(&app->store,
            (gs232b_axis_id_t)i, &zero);
        app->config.axes[i].zero_valid = app->status.zero_error[i] == GS232B_OK;
        app->config.axes[i].zero_uv = zero;
    }
    gs232b_error_t error = gs232b_motion_init(&app->motion, &app->outputs, sampler, &app->config);
    if (error != GS232B_OK) return fail(app, error);
    error = gs232b_dispatcher_init(&app->dispatcher, &app->motion, app->store,
        p->io, app->requests, sizeof(app->requests));
    if (error != GS232B_OK) return fail(app, error);
    app->status.network_error = p->network_bind(p->ctx, app);
    app->status.uart_error = p->uart_open(p->ctx, app->store);
    app->status.phase = GS232B_APP_UART_BOOT;
    return GS232B_OK;
}

gs232b_error_t gs232b_app_step(gs232b_app_t *app)
{
    if (app == NULL || app->status.phase == GS232B_APP_COLD) return GS232B_ERROR_NOT_READY;
    if (app->status.phase == GS232B_APP_FAILED) return app->status.fatal_error;
    gs232b_app_platform_t *p = &app->platform;
    if (app->status.phase == GS232B_APP_RESET_WAIT) {
        const gs232b_error_t error = gs232b_w5500_reset_tick(&app->startup_reset);
        if (error != GS232B_OK) return fail(app, error);
        return app->startup_reset.phase == GS232B_W5500_READY ? initialize(app) : GS232B_OK;
    }
    if (app->status.phase == GS232B_APP_UART_BOOT) {
        if (app->status.uart_error == GS232B_OK) {
            gs232b_error_t error = p->uart_boot(p->ctx);
            if (error == GS232B_ERROR_WOULD_BLOCK) return GS232B_OK;
            if (error == GS232B_OK) error = p->uart_attach(p->ctx, &app->dispatcher);
            if (error == GS232B_ERROR_WOULD_BLOCK) return GS232B_OK;
            app->status.uart_error = error;
            app->status.uart_ready = error == GS232B_OK;
        }
        /* NOTE: UART 先接纳；任何网络启动失败都不能跳过后续 UART service。 */
        const gs232b_error_t network = p->network_start(p->ctx, app);
        if (network != GS232B_OK) app->status.network_error = network;
        app->status.phase = GS232B_APP_RUNNING;
    }
    if (app->status.uart_ready) (void)p->uart_service(p->ctx);
    const gs232b_error_t network = p->network_service(p->ctx, app);
    if (network != GS232B_ERROR_WOULD_BLOCK) app->status.network_error = network;
    const gs232b_error_t error = gs232b_dispatcher_tick(&app->dispatcher);
    if (app->status.uart_ready) (void)p->uart_service(p->ctx);
    return error;
}

gs232b_app_status_t gs232b_app_status(const gs232b_app_t *app)
{
    return app == NULL ? (gs232b_app_status_t){.fatal_error = GS232B_ERROR_INVALID_ARGUMENT} :
                         app->status;
}
