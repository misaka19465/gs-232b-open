#ifndef UART_BOOT_TEST_SUPPORT_H
#define UART_BOOT_TEST_SUPPORT_H

#include "gs232b/esp_uart.h"
#include "esp_uart_internal.h"
#include "uart_api.h"
#include "rmt_api.h"
#include "gs232b/fakes.h"
#include "motion_test_support.h"

#include <string.h>

typedef struct {
    gs232b_esp_uart_t uart;
    gs232b_fake_store_t store;
    gs232b_motion_sim_t sim;
    gs232b_dispatcher_t dispatcher;
    uint8_t requests[GS232B_LINE_STORAGE_BYTES];
    uint8_t line[GS232B_LINE_STORAGE_BYTES];
} fixture_t;

typedef struct {
    rmt_symbol_word_t symbols[GS232B_UART_CAPTURE_SYMBOLS + 1];
    size_t pulses;
} waveform_t;

static inline void pulse(waveform_t *wave, bool high, unsigned us)
{
    rmt_symbol_word_t *symbol = &wave->symbols[wave->pulses / 2];
    if ((wave->pulses++ & 1U) == 0) {
        symbol->level0 = high;
        symbol->duration0 = us;
    } else {
        symbol->level1 = high;
        symbol->duration1 = us;
    }
}

/* NOTE: 测试从 UART 起始位及八个 LSB-first 数据位独立生成边沿，
 * 不从生产判定器的运行长度表反推预期波形。 */
static inline void character(waveform_t *wave, uint32_t baud, uint8_t byte, unsigned stop_us,
                       int adjustment)
{
    const unsigned bit_us = (unsigned)((1000000U + baud / 2U) / baud);
    bool level = false;
    unsigned width = bit_us;
    for (unsigned bit = 0; bit < 8; ++bit) {
        const bool next = ((byte >> bit) & 1U) != 0;
        if (next == level) { width += bit_us; }
        else { pulse(wave, level, (unsigned)((int)width + adjustment)); level = next; width = bit_us; }
    }
    if (level) { pulse(wave, true, width + stop_us); }
    else {
        pulse(wave, false, (unsigned)((int)width + adjustment));
        if (stop_us != 0) { pulse(wave, true, stop_us); }
    }
}

static inline waveform_t crs(uint32_t baud, unsigned count, bool idle_stop)
{
    waveform_t wave = {0};
    const unsigned bit = (1000000U + baud / 2U) / baud;
    for (unsigned i = 0; i < count; ++i) {
        character(&wave, baud, '\r', idle_stop && i == count - 1 ? 12000 : bit, 0);
    }
    return wave;
}

static inline void capture(const waveform_t *wave)
{
    fake_uart.now_us += 20000;
    fake_rmt_complete(wave->symbols, (wave->pulses + 1) / 2, true);
}

static inline bool start(fixture_t *f)
{
    fake_uart_reset();
    memset(f, 0, sizeof(*f));
    gs232b_fake_store_init(&f->store);
    CHECK(gs232b_esp_uart_open(&f->uart, gs232b_fake_store_port(&f->store)) == GS232B_OK);
    return true;
}

static inline bool finish(fixture_t *f)
{
    CHECK(gs232b_esp_uart_close(&f->uart) == GS232B_OK);
    CHECK(!fake_rmt.allocated && !fake_uart.installed && fake_uart.live_queues == 0 &&
          fake_uart.live_tasks == 0 && fake_uart.live_semaphores == 0);
    return true;
}

static inline bool open_dispatcher(fixture_t *f)
{
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    CHECK(gs232b_motion_sim_init(&f->sim, &config, 0, WAIT_US) == GS232B_OK);
    CHECK(gs232b_dispatcher_init(&f->dispatcher, &f->sim.motion,
        gs232b_fake_store_port(&f->store), gs232b_esp_uart_io(&f->uart),
        f->requests, sizeof(f->requests)) == GS232B_OK);
    return true;
}

#endif
