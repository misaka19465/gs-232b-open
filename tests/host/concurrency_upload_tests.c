#include "concurrency_test_support.h"

static bool maximum_slow_upload(char command, bool uart)
{
    concurrency_fixture_t f;
    uint8_t line[GS232B_GS_LINE_MAX_BYTES];
    CHECK(concurrency_init(&f));
    memset(line, ' ', sizeof(line));
    memcpy(line, command == 'M' ? "M010" : "W010", 4);
    for (size_t i = 0; i < GS232B_POSITION_MAX_ANGLES; ++i) memcpy(line + 4 + i * 4, " 090", 4);
    const gs232b_time_us_t start = f.network.sim.now_us;
    const gs232b_session_key_t uploader = uart ? f.uart.session : ws_client(&f.network, 0)->session;
    for (size_t offset = 0; offset < sizeof(line);) {
        const size_t size = sizeof(line) - offset < 113 ? sizeof(line) - offset : 113;
        concurrency_time(&f, f.network.sim.now_us + UINT64_C(1000000));
        if (uart) fake_uart_input(line + offset, size);
        else CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, true, line + offset, size) == ESP_OK);
        for (int fd = uart ? 0 : 1; fd < (int)GS232B_WS_CLIENTS; ++fd) {
            CHECK(fake_ws_frame(fd, HTTPD_WS_TYPE_BINARY, true, "C\r", 2) == ESP_OK);
        }
        if (!uart) fake_uart_input("C\r", 2);
        CHECK(concurrency_poll(&f, 8));
        for (int fd = 0; fd < (int)GS232B_WS_CLIENTS; ++fd) {
            CHECK(concurrency_net_expect(fd, !uart && fd == 0 ? "" : "AZ=090\r\n"));
        }
        CHECK(concurrency_uart_expect(uart ? "" : "AZ=090\r\n"));
        CHECK(stopped(&f.network.sim));
        CHECK(!gs232b_session_key_valid(f.network.dispatcher.control.owner));
        CHECK(f.network.dispatcher.control.sequence.active.point_count == 0);
        CHECK(gs232b_session_key_equal(f.network.dispatcher.control.input_reservation.session, uploader));
        offset += size;
    }
    CHECK(f.network.sim.now_us - start > UINT64_C(137000000));
    CHECK(f.network.dispatcher.control.sessions[uploader.session_id - 1U].parser.length == sizeof(line));
    if (uart) CHECK(concurrency_uart_input(&f, "\r"));
    else CHECK(fake_ws_frame(0, HTTPD_WS_TYPE_BINARY, true, "\r", 1) == ESP_OK);
    CHECK(concurrency_poll(&f, 8));
    CHECK(concurrency_uart_expect(uart ? "\r" : ""));
    CHECK(concurrency_net_expect(0, uart ? "" : "\r"));
    CHECK(f.network.dispatcher.control.sequence.active.point_count ==
        (command == 'M' ? GS232B_M_SEQUENCE_MAX_POINTS : GS232B_W_SEQUENCE_MAX_PAIRS));
    CHECK(f.network.dispatcher.control.request_bytes_used == 0);
    CHECK(!gs232b_session_key_valid(f.network.dispatcher.control.input_reservation.session));
    CHECK(concurrency_net_command(&f, 3, "S\r", "\r"));
    return concurrency_close(&f);
}

bool concurrency_upload_tests(void)
{
    return maximum_slow_upload('M', false) && maximum_slow_upload('W', true);
}
