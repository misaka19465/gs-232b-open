/*
 * GS-232B Open - rotator controller firmware.
 * Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "uart_boot_test_support.h"

static bool test_bad_width_stop_and_truncated_capture(void)
{
    for (unsigned fault = 0; fault < 8; ++fault) {
        fixture_t f;
        CHECK(start(&f));
        waveform_t wave = crs(9600, 2, true);
        capture(&wave);
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
        wave = crs(9600, 1, true);
        if (fault == 0) { wave.symbols[0].duration0 = 115; } /* 超出 +8% */
        if (fault == 1) { wave.symbols[2].duration0 = 300; } /* 少一个数据零位 */
        if (fault == 2) { wave.symbols[2].duration1 = 50; } /* 停止位过短 */
        if (fault == 3) { wave.symbols[1].level1 = 0; } /* 丢边沿/同电平重复 */
        if (fault == 4) {
            /* NOTE: 超出缓冲容量是 API 契约故障，即使开头含好 CR 也整批拒绝。 */
            wave = crs(9600, 3, true);
            fake_uart.now_us += 20000;
            fake_rmt_complete(wave.symbols, GS232B_UART_CAPTURE_SYMBOLS + 1, true);
        } else if (fault == 7) {
            fake_uart.now_us += 20000;
            fake_rmt.wrong_buffer = true;
            fake_rmt_complete(wave.symbols, 3, true);
            fake_rmt.wrong_buffer = false;
        } else if (fault == 5) {
            fake_uart.now_us += 20000;
            wave.symbols[2].duration1 = 0; /* partial 块不得含 EOF */
            fake_rmt_complete(wave.symbols, 3, false);
        } else if (fault == 6) {
            wave.symbols[0].duration1 = 0; /* EOF 后还存在非零数据 */
            capture(&wave);
        } else { capture(&wave); }
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
        CHECK(!f.uart.boot.ready && fake_uart.baud_changes == 0);
        wave = crs(9600, 2, true);
        capture(&wave);
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
        wave = crs(9600, 1, true);
        capture(&wave);
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK && f.uart.boot.matched);
        CHECK(finish(&f));
    }
    return true;
}

static bool test_streaming_chunks_and_queue_gaps(void)
{
    fixture_t f;
    CHECK(start(&f));
    waveform_t wave = crs(2400, 3, false);
    /* NOTE: 人为在 CR 内部切片，部分块不能作为捕获边界丢弃半字符。 */
    fake_rmt_complete(wave.symbols, 4, false);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    memset(fake_rmt.buffer, 0, fake_rmt.buffer_size); /* 模拟 SDK 立即复用 */
    fake_rmt_complete(wave.symbols + 4, 5, false);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK);
    CHECK(f.uart.boot.matched && f.uart.boot.baud == 2400);
    CHECK(finish(&f));

    CHECK(start(&f));
    wave = crs(4800, 2, false);
    fake_rmt_complete(wave.symbols, 6, false);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    const waveform_t empty = {0};
    fake_rmt_complete(empty.symbols, 0, false); /* 未消费块即将被覆盖 */
    wave = crs(4800, 1, false);
    fake_rmt_complete(wave.symbols, 3, false);
    memset(fake_rmt.buffer, 0xff, fake_rmt.buffer_size);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(f.uart.capture.detector.complete_crs == 1); /* 不能继承缺口前两个 CR */
    fake_rmt_complete(wave.symbols, 3, false);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    fake_rmt_complete(wave.symbols, 3, false);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK && f.uart.boot.baud == 4800);
    CHECK(finish(&f));

    CHECK(start(&f));
    fake_rmt_complete(empty.symbols, 0, false);
    fake_rmt_complete(empty.symbols, 0, true); /* last 必须保留，使通道可重启 */
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(fake_rmt.receiving && fake_rmt.receives == 2);
    wave = crs(9600, 16, false); /* 填满一个真实 partial 用户缓冲块 */
    fake_rmt_complete(wave.symbols, GS232B_UART_CAPTURE_SYMBOLS, false);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK && f.uart.boot.baud == 9600);
    return finish(&f);
}

static bool test_capture_setup_and_rearm_failures(void)
{
    fixture_t f = {0};
    gs232b_fake_store_init(&f.store);
    for (int stage = 1; stage <= 4; ++stage) {
        fake_uart_reset();
        fake_rmt.fail_init_at = stage;
        CHECK(gs232b_esp_uart_open(&f.uart, gs232b_fake_store_port(&f.store)) == GS232B_ERROR_IO);
        CHECK(finish(&f));
    }
    CHECK(start(&f));
    waveform_t empty = {0};
    capture(&empty);
    fake_rmt.fail_receive = true;
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_IO);
    CHECK(!f.uart.boot.ready);
    fake_rmt.fail_receive = false;
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(fake_rmt.receiving);
    return finish(&f);
}

static bool test_quantization_jitter_and_byte_corpus(void)
{
    const uint32_t rates[] = {1200, 2400, 4800, 9600};
    for (size_t rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
        for (int drift = -7; drift <= 7; drift += 7) {
            fixture_t f;
            CHECK(start(&f));
            waveform_t wave = crs(rates[rate], 3, false);
            for (size_t i = 0; i < (wave.pulses + 1) / 2; ++i) {
                wave.symbols[i].duration0 = (wave.symbols[i].duration0 * (100 + drift) + 50) / 100;
                wave.symbols[i].duration1 = (wave.symbols[i].duration1 * (100 + drift) + 50) / 100;
            }
            capture(&wave);
            CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK);
            CHECK(f.uart.boot.matched && f.uart.boot.baud == rates[rate]);
            CHECK(finish(&f));
        }
    }
    fixture_t f;
    CHECK(start(&f));
    for (unsigned byte = 0; byte < 256; ++byte) {
        if (byte == '\r') { continue; }
        waveform_t wave = {0};
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
            character(&wave, 9600, (uint8_t)byte, repeat == 2 ? 12000 : 104, 0);
        }
        capture(&wave);
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
        CHECK(!f.uart.boot.ready);
    }
    const waveform_t unsupported = crs(19200, 3, true);
    capture(&unsupported);
    CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    return finish(&f);
}

static bool test_partial_cannot_cross_capture_boundary(void)
{
    for (unsigned prefix = 1; prefix <= 5; ++prefix) {
        fixture_t f;
        CHECK(start(&f));
        waveform_t wave = crs(1200, 2, true);
        capture(&wave);
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
        wave = crs(1200, 1, false);
        wave.pulses = prefix;
        if (prefix & 1U) { wave.symbols[prefix / 2].duration1 = 0; }
        fake_rmt.rx_high = false;
        capture(&wave);
        fake_rmt.rx_high = true;
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
        CHECK(f.uart.capture.detector.complete_crs == 0);
        wave = crs(1200, 2, true);
        capture(&wave);
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
        wave = crs(1200, 1, true);
        capture(&wave);
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_OK && f.uart.boot.baud == 1200);
        CHECK(finish(&f));
    }
    return true;
}

static bool test_idle_eof_proves_only_stop_bit(void)
{
    const uint32_t rates[] = {1200, 2400, 4800, 9600};
    for (size_t rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
        fixture_t f;
        CHECK(start(&f));
        waveform_t wave = {0};
        character(&wave, rates[rate], '\r', 0, 0);
        CHECK(wave.pulses == 5); /* RMT 省略 idle 高电平的普通时长，留下 EOF */
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
            capture(&wave);
            CHECK(gs232b_esp_uart_boot_service(&f.uart) ==
                (repeat == 2 ? GS232B_OK : GS232B_ERROR_WOULD_BLOCK));
        }
        CHECK(f.uart.boot.matched && f.uart.boot.baud == rates[rate]);
        CHECK(finish(&f));
    }
    fixture_t f;
    CHECK(start(&f));
    waveform_t short_data = crs(9600, 1, true);
    short_data.symbols[2].duration0 = 300; /* idle 只能证明停止位，不能补齐数据 */
    short_data.symbols[2].duration1 = 0;
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        capture(&short_data);
        CHECK(gs232b_esp_uart_boot_service(&f.uart) == GS232B_ERROR_WOULD_BLOCK);
    }
    return finish(&f);
}

int main(void)
{
    return test_bad_width_stop_and_truncated_capture() && test_capture_setup_and_rearm_failures() &&
        test_quantization_jitter_and_byte_corpus() && test_partial_cannot_cross_capture_boundary() &&
        test_streaming_chunks_and_queue_gaps() && test_idle_eof_proves_only_stop_bit()
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
