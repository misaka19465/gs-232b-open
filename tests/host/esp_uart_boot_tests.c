#include "uart_boot_test_support.h"

static bool test_all_rates_and_single_banner(void)
{
    const uint32_t rates[] = {1200, 2400, 4800, 9600};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        fixture_t f;
        CHECK(start(&f));
        CHECK(open_dispatcher(&f));
        CHECK(fake_rmt.config.gpio_num == 7 && fake_rmt.config.clk_src == RMT_CLK_SRC_APB);
        CHECK(fake_rmt.config.resolution_hz == 1000000 && fake_rmt.config.mem_block_symbols == 48);
        CHECK(!fake_rmt.config.flags.with_dma && !fake_rmt.config.flags.invert_in);
        CHECK(fake_rmt.buffer_size == 48 * sizeof(rmt_symbol_word_t));
        CHECK(fake_rmt.receive_config.signal_range_max_ns == 12000000);
        CHECK(fake_rmt.receive_config.flags.en_partial_rx);
        CHECK(gs232b_esp_uart_attach(&f.uart, &f.dispatcher, f.line, sizeof(f.line)) ==
              GS232B_ERROR_NOT_READY);
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
        CHECK(fake_uart.wire_size == 0 && fake_uart.baud_changes == 0);
        /* NOTE: UART 在错误速率收到的杂字节/故障不能绕过捕获确认。 */
        fake_uart_input("R\r\r\r", 4);
        fake_uart_event(UART_FRAME_ERR);
        const waveform_t first = crs(rates[i], 2, true);
        capture(&first);
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
        CHECK(fake_uart.baud_changes == 0 && f.uart.capture.detector.complete_crs == 2);
        const waveform_t last = crs(rates[i], 1, true);
        capture(&last);
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK);
        const gs232b_uart_boot_status_t status = gs232b_esp_uart_boot_status(&f.uart);
        CHECK(status.ready && status.matched && status.baud == rates[i]);
        CHECK(status.save_error == GS232B_OK && status.saved_baud == rates[i]);
        CHECK(fake_uart.config.baud_rate == (int)rates[i] && fake_uart.baud_changes == 1);
        CHECK(!fake_rmt.allocated && fake_uart.rx_count == 0 &&
              uxQueueMessagesWaiting(f.uart.events) == 0);
        uint32_t saved;
        uint64_t revision;
        const gs232b_store_t store = gs232b_fake_store_port(&f.store);
        CHECK(gs232b_uart_baud_load(&store, &saved, &revision) == GS232B_OK);
        CHECK(saved == rates[i] && revision == 1);
        CHECK(gs232b_esp_uart_attach(&f.uart, &f.dispatcher, f.line, sizeof(f.line)) == GS232B_OK);
        for (unsigned repeat = 0; repeat < 10; ++repeat) {
            CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK);
            CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
        }
        CHECK(fake_uart.wire_size == 12 && memcmp(fake_uart.wire, "Connect OK\r\n", 12) == 0);
        CHECK(gs232b_esp_uart_attach(&f.uart, &f.dispatcher, f.line, sizeof(f.line)) ==
              GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(gs232b_uart_baud_load(&store, &saved, &revision) == GS232B_OK && revision == 1);
        CHECK(fake_uart.baud_changes == 1);
        CHECK(f.dispatcher.control.request_count == 0);
        CHECK(finish(&f));
    }
    return true;
}

static bool test_noise_incomplete_and_mixed_rates(void)
{
    fixture_t f;
    CHECK(start(&f));
    waveform_t wave = crs(2400, 2, true);
    capture(&wave);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    wave = (waveform_t){0};
    pulse(&wave, false, 5); pulse(&wave, true, 9);
    character(&wave, 2400, '\r', 12000, 0);
    capture(&wave);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(f.uart.capture.detector.complete_crs == 1);
    wave = (waveform_t){0};
    character(&wave, 2400, '\r', 0, 0); /* 残缺停止位 */
    fake_rmt.rx_high = false;
    capture(&wave);
    fake_rmt.rx_high = true;
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(f.uart.capture.detector.complete_crs == 0);
    wave = crs(2400, 2, true);
    capture(&wave);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    wave = crs(4800, 2, true);
    capture(&wave);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(f.uart.capture.detector.complete_crs == 2 &&
          f.uart.capture.detector.previous == 4800);
    wave = (waveform_t){0};
    character(&wave, 4800, 'C', 12000, 0);
    capture(&wave);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(f.uart.capture.detector.complete_crs == 0);
    wave = crs(4800, 3, true);
    capture(&wave);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK);
    CHECK(f.uart.boot.matched && f.uart.boot.baud == 4800);
    return finish(&f);
}

static bool test_timeout_reboot_and_deadline(void)
{
    fixture_t f;
    CHECK(start(&f));
    CHECK(open_dispatcher(&f));
    fake_uart.now_us = (int64_t)GS232B_UART_BAUD_WINDOW_US - 1;
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(!f.uart.boot.ready && fake_uart.wire_size == 0);
    ++fake_uart.now_us;
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK);
    CHECK(f.uart.boot.ready && !f.uart.boot.matched && f.uart.boot.baud == 9600);
    CHECK(!f.store.records[GS232B_STORE_UART_BAUD].present);
    CHECK(gs232b_esp_uart_attach(&f.uart, &f.dispatcher, f.line, sizeof(f.line)) == GS232B_OK);
    for (unsigned i = 0; i < 10; ++i) {
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK);
        CHECK(gs232b_esp_uart_service(&f.uart) == GS232B_OK);
    }
    CHECK(fake_uart.wire_size == 12 && memcmp(fake_uart.wire, "Connect OK\r\n", 12) == 0);
    CHECK(finish(&f));

    const gs232b_store_t store = gs232b_fake_store_port(&f.store);
    uint64_t revision;
    CHECK(gs232b_uart_baud_commit(&store, 0, 1200, &revision) == GS232B_OK);
    for (unsigned boundary = 0; boundary < 3; ++boundary) {
        fake_uart_reset();
        CHECK(gs232b_esp_uart_open(&f.uart, store) == GS232B_OK);
        CHECK(fake_uart.config.baud_rate == 1200);
        const waveform_t wave = crs(4800, 3, true);
        fake_uart.now_us = (int64_t)GS232B_UART_BAUD_WINDOW_US + (int64_t)boundary - 1;
        fake_rmt_complete(wave.symbols, (wave.pulses + 1) / 2, true);
        /* NOTE: 最早一批完成于截止前，即使控制任务稍晚消费也应有效。 */
        fake_uart.now_us += 100;
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK);
        CHECK(f.uart.boot.baud == (boundary == 0 ? 4800 : 1200));
        CHECK(f.uart.boot.matched == (boundary == 0));
        CHECK(finish(&f));
        CHECK(gs232b_uart_baud_load(&store, &f.uart.boot.saved_baud, &revision) == GS232B_OK);
        CHECK(gs232b_uart_baud_commit(&store, revision, 1200, &revision) == GS232B_OK);
    }
    return true;
}

static bool test_save_failures_and_retry_barriers(void)
{
    fixture_t f;
    CHECK(start(&f));
    const gs232b_store_t store = gs232b_fake_store_port(&f.store);
    uint64_t revision;
    CHECK(gs232b_uart_baud_commit(&store, 0, 2400, &revision) == GS232B_OK);
    CHECK(finish(&f));
    fake_uart_reset();
    CHECK(gs232b_esp_uart_open(&f.uart, store) == GS232B_OK);
    CHECK(gs232b_fake_fail_next(&f.store.failures[GS232B_STORE_UART_BAUD][GS232B_FAKE_STORE_SAVE],
        GS232B_ERROR_IO, 1) == GS232B_OK);
    const waveform_t wave = crs(9600, 3, true);
    capture(&wave);
    fake_rmt.fail_disable = true;
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_IO);
    CHECK(!f.uart.boot.ready && fake_uart.baud_changes == 0);
    fake_rmt.fail_disable = false;
    fake_rmt.fail_delete = true;
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_IO);
    fake_rmt.fail_delete = false;
    fake_uart.fail_baud = true;
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_IO);
    fake_uart.fail_baud = false;
    fake_uart.fail_flush = true;
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_IO);
    CHECK(!f.uart.boot.ready && f.uart.boot.save_error == GS232B_ERROR_IO);
    fake_uart.fail_flush = false;
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK);
    CHECK(f.uart.boot.matched && f.uart.boot.baud == 9600 && f.uart.boot.saved_baud == 2400);
    uint32_t saved;
    CHECK(gs232b_uart_baud_load(&store, &saved, &revision) == GS232B_OK && saved == 2400);
    CHECK(fake_uart.baud_changes == 1);
    CHECK(finish(&f));
    fake_uart_reset();
    CHECK(gs232b_esp_uart_open(&f.uart, store) == GS232B_OK);
    fake_uart.now_us += (int64_t)GS232B_UART_BAUD_WINDOW_US;
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK && f.uart.boot.baud == 2400);
    return finish(&f);
}

static bool test_same_rate_skips_flash_and_load_failure_falls_back(void)
{
    fixture_t f;
    CHECK(start(&f));
    CHECK(finish(&f));
    const gs232b_store_t store = gs232b_fake_store_port(&f.store);
    uint64_t revision;
    CHECK(gs232b_uart_baud_commit(&store, 0, 4800, &revision) == GS232B_OK);
    fake_uart_reset();
    CHECK(gs232b_esp_uart_open(&f.uart, store) == GS232B_OK);
    CHECK(gs232b_fake_fail_next(&f.store.failures[GS232B_STORE_UART_BAUD][GS232B_FAKE_STORE_SAVE],
        GS232B_ERROR_IO, 1) == GS232B_OK);
    const waveform_t wave = crs(4800, 3, true);
    capture(&wave);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK);
    CHECK(f.uart.boot.save_error == GS232B_OK);
    CHECK(f.store.failures[GS232B_STORE_UART_BAUD][GS232B_FAKE_STORE_SAVE].remaining == 1);
    CHECK(finish(&f));
    CHECK(gs232b_fake_fail_next(&f.store.failures[GS232B_STORE_UART_BAUD][GS232B_FAKE_STORE_LOAD],
        GS232B_ERROR_IO, 1) == GS232B_OK);
    fake_uart_reset();
    CHECK(gs232b_esp_uart_open(&f.uart, store) == GS232B_OK);
    CHECK(f.uart.boot.load_error == GS232B_ERROR_IO && f.uart.boot.saved_baud == 9600);
    fake_uart.now_us += (int64_t)GS232B_UART_BAUD_WINDOW_US;
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK);
    CHECK(f.uart.boot.baud == 9600 && !f.uart.boot.matched);
    uint32_t saved;
    CHECK(gs232b_uart_baud_load(&store, &saved, &revision) == GS232B_OK && saved == 4800);
    return finish(&f);
}

int main(void)
{
    return test_all_rates_and_single_banner() && test_noise_incomplete_and_mixed_rates() &&
        test_timeout_reboot_and_deadline() && test_save_failures_and_retry_barriers() &&
        test_same_rate_skips_flash_and_load_failure_falls_back()
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
