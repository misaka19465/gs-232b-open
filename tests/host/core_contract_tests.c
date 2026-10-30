#include "gs232b/fakes.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static bool test_types_and_epoch(void)
{
    gs232b_state_snapshot_t state;
    gs232b_state_snapshot_init(&state);
    CHECK(state.command_epoch == 1);
    CHECK(state.axes[GS232B_AXIS_AZIMUTH].axis == GS232B_AXIS_AZIMUTH);
    CHECK(state.axes[GS232B_AXIS_ELEVATION].axis == GS232B_AXIS_ELEVATION);
    CHECK(state.axes[GS232B_AXIS_AZIMUTH].minimum_mdeg == 0);
    CHECK(state.axes[GS232B_AXIS_AZIMUTH].maximum_mdeg == 450000);
    CHECK(state.axes[GS232B_AXIS_ELEVATION].minimum_mdeg == 0);
    CHECK(state.axes[GS232B_AXIS_ELEVATION].maximum_mdeg == 180000);
    CHECK(!state.output.known);
    CHECK(state.output.requested.azimuth == GS232B_AZ_STOP);
    CHECK(state.output.requested.elevation == GS232B_EL_STOP);
    CHECK(state.output.requested.w5500_reset_asserted);

    gs232b_session_key_t first = {.session_id = 4, .generation = 2};
    gs232b_session_key_t stale = {.session_id = 4, .generation = 1};
    gs232b_command_context_t command = {.session = first, .epoch = 1};
    CHECK(gs232b_command_context_current(command, first, 1));
    CHECK(!gs232b_command_context_current(command, stale, 1));
    CHECK(!gs232b_command_context_current(command, first, 2));
    CHECK(gs232b_session_key_equal(first, first));
    CHECK(!gs232b_session_key_equal(first, stale));

    CHECK(gs232b_command_epoch_advance(&state.command_epoch) == GS232B_OK);
    CHECK(state.command_epoch == 2);
    state.command_epoch = UINT64_MAX;
    CHECK(gs232b_command_epoch_advance(&state.command_epoch) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(state.command_epoch == UINT64_MAX);
    CHECK(strcmp(gs232b_error_name(GS232B_ERROR_NOT_OWNER), "NOT_OWNER") == 0);
    return true;
}

static bool test_virtual_clock_sampler_and_output(void)
{
    gs232b_fake_clock_t clock_fake;
    gs232b_fake_clock_init(&clock_fake, 100);
    gs232b_clock_t clock = gs232b_fake_clock_port(&clock_fake);
    CHECK(clock.now_us(clock.ctx) == 100);

    gs232b_fake_sampler_t sampler_fake;
    CHECK(gs232b_fake_sampler_init(&sampler_fake, clock) == GS232B_OK);
    CHECK(gs232b_fake_sampler_set(&sampler_fake, GS232B_AXIS_AZIMUTH, 101, 400000) == GS232B_OK);
    CHECK(gs232b_fake_clock_advance(&clock_fake, 20000) == GS232B_OK);
    CHECK(gs232b_fake_sampler_set(&sampler_fake, GS232B_AXIS_ELEVATION, 202, 800000) == GS232B_OK);

    gs232b_sampler_t sampler = gs232b_fake_sampler_port(&sampler_fake);
    gs232b_sample_t sample = {0};
    CHECK(gs232b_sampler_read(&sampler, GS232B_AXIS_AZIMUTH, &sample) == GS232B_OK);
    CHECK(sample.raw == 101 && sample.adc_uv == 400000 && sample.acquired_at_us == 100);
    CHECK(gs232b_sampler_read(&sampler, GS232B_AXIS_ELEVATION, &sample) == GS232B_OK);
    CHECK(sample.raw == 202 && sample.adc_uv == 800000 && sample.acquired_at_us == 20100);

    gs232b_fake_output_t output_fake;
    CHECK(gs232b_fake_output_init(&output_fake, clock) == GS232B_OK);
    gs232b_output_t output = gs232b_fake_output_port(&output_fake);
    gs232b_output_frame_t az_and_el = {
        .azimuth = GS232B_AZ_CW,
        .elevation = GS232B_EL_UP,
        .speed_permille = 500,
        .w5500_reset_asserted = false
    };
    CHECK(gs232b_output_apply(&output, &az_and_el) == GS232B_OK);
    CHECK(gs232b_fake_clock_advance(&clock_fake, 50000) == GS232B_OK);
    gs232b_output_frame_t reversed_el = az_and_el;
    reversed_el.elevation = GS232B_EL_DOWN;
    reversed_el.speed_permille = 750;
    CHECK(gs232b_output_apply(&output, &reversed_el) == GS232B_OK);
    CHECK(gs232b_fake_clock_advance(&clock_fake, 15000) == GS232B_OK);
    gs232b_output_frame_t stop = {
        .azimuth = GS232B_AZ_STOP,
        .elevation = GS232B_EL_STOP,
        .speed_permille = 0,
        .w5500_reset_asserted = false
    };
    CHECK(gs232b_output_apply(&output, &stop) == GS232B_OK);

    CHECK(output_fake.trace_count == 3);
    CHECK(output_fake.trace[0].at_us == 20100);
    CHECK(output_fake.trace[0].frame.azimuth == GS232B_AZ_CW);
    CHECK(output_fake.trace[0].frame.elevation == GS232B_EL_UP);
    CHECK(output_fake.trace[1].at_us == 70100);
    CHECK(output_fake.trace[1].frame.elevation == GS232B_EL_DOWN);
    CHECK(output_fake.trace[2].at_us == 85100);
    CHECK(output_fake.trace[2].frame.azimuth == GS232B_AZ_STOP);
    CHECK(output_fake.trace[2].frame.elevation == GS232B_EL_STOP);
    CHECK(output_fake.snapshot.known);
    CHECK(output_fake.snapshot.last_successful.azimuth == GS232B_AZ_STOP);

    CHECK(gs232b_fake_fail_next(&output_fake.failure, GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs232b_fake_clock_advance(&clock_fake, 1000) == GS232B_OK);
    CHECK(gs232b_output_apply(&output, &az_and_el) == GS232B_ERROR_IO);
    CHECK(output_fake.trace_count == 4);
    CHECK(output_fake.trace[3].result == GS232B_ERROR_IO);
    CHECK(!output_fake.snapshot.known);
    CHECK(output_fake.snapshot.last_successful.azimuth == GS232B_AZ_STOP);
    CHECK(output_fake.snapshot.last_error == GS232B_ERROR_IO);

    gs232b_output_frame_t invalid = az_and_el;
    invalid.speed_permille = 1001;
    CHECK(gs232b_output_apply(&output, &invalid) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(output_fake.trace_count == 4);

    CHECK(gs232b_fake_clock_advance(&clock_fake, UINT64_MAX) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(clock.now_us(clock.ctx) == 86100);
    return true;
}

static bool test_store_atomic_failure_injection(void)
{
    gs232b_fake_store_t store_fake;
    gs232b_fake_store_init(&store_fake);
    gs232b_store_t store = gs232b_fake_store_port(&store_fake);
    static const uint8_t initial[] = {0x47, 0x53, 0x01, 0x00};
    static const uint8_t replacement[] = {0x47, 0x53, 0x02, 0x00, 0xff};
    uint8_t buffer[8] = {0};
    size_t size = 99;

    CHECK(gs232b_store_load(&store, GS232B_STORE_NETWORK, NULL, 0, &size) ==
          GS232B_ERROR_NOT_FOUND);
    CHECK(size == 0);
    CHECK(gs232b_store_save(&store, GS232B_STORE_ZERO_AZ, initial, sizeof(initial)) == GS232B_OK);
    CHECK(gs232b_fake_fail_next(&store_fake.failures[GS232B_STORE_ZERO_AZ][GS232B_FAKE_STORE_SAVE],
                                GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs232b_store_save(&store, GS232B_STORE_ZERO_AZ,
                            replacement, sizeof(replacement)) == GS232B_ERROR_IO);
    CHECK(gs232b_store_load(&store, GS232B_STORE_ZERO_AZ, NULL, 0, &size) ==
          GS232B_ERROR_BUFFER_TOO_SMALL);
    CHECK(size == sizeof(initial));
    CHECK(gs232b_store_load(&store, GS232B_STORE_ZERO_AZ, buffer, sizeof(buffer), &size) ==
          GS232B_OK);
    CHECK(size == sizeof(initial) && memcmp(buffer, initial, size) == 0);
    CHECK(gs232b_fake_fail_next(&store_fake.failures[GS232B_STORE_ZERO_AZ][GS232B_FAKE_STORE_LOAD],
                                GS232B_ERROR_TIMEOUT, 1) == GS232B_OK);
    CHECK(gs232b_store_load(&store, GS232B_STORE_ZERO_AZ, buffer, sizeof(buffer), &size) ==
          GS232B_ERROR_TIMEOUT);
    CHECK(size == 0);
    CHECK(gs232b_store_load(&store, GS232B_STORE_ZERO_AZ, buffer, sizeof(buffer), &size) ==
          GS232B_OK);
    CHECK(size == sizeof(initial) && memcmp(buffer, initial, size) == 0);

    CHECK(gs232b_store_save(&store, GS232B_STORE_ZERO_EL, replacement, sizeof(replacement)) ==
          GS232B_OK);
    CHECK(gs232b_fake_fail_next(&store_fake.failures[GS232B_STORE_ZERO_EL][GS232B_FAKE_STORE_ERASE],
                                GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs232b_store_erase(&store, GS232B_STORE_ZERO_EL) == GS232B_ERROR_IO);
    CHECK(gs232b_store_load(&store, GS232B_STORE_ZERO_EL, buffer, sizeof(buffer), &size) ==
          GS232B_OK);
    CHECK(size == sizeof(replacement) && memcmp(buffer, replacement, size) == 0);

    CHECK(gs232b_store_save(&store, GS232B_STORE_DISPLAY, NULL, 0) == GS232B_OK);
    CHECK(gs232b_store_load(&store, GS232B_STORE_DISPLAY, NULL, 0, &size) == GS232B_OK);
    CHECK(size == 0);
    CHECK(gs232b_store_save(&store, GS232B_STORE_DISPLAY, replacement,
                            GS232B_FAKE_STORE_RECORD_CAPACITY + 1) ==
          GS232B_ERROR_RESOURCE_EXHAUSTED);
    return true;
}

static bool test_multiple_sessions_and_generations(void)
{
    gs232b_fake_clock_t clock_fake;
    gs232b_fake_clock_init(&clock_fake, 5000);
    gs232b_clock_t clock = gs232b_fake_clock_port(&clock_fake);
    gs232b_fake_sessions_t sessions_fake;
    CHECK(gs232b_fake_sessions_init(&sessions_fake, clock) == GS232B_OK);
    gs232b_session_io_t io = gs232b_fake_sessions_port(&sessions_fake);

    gs232b_session_key_t uart, ethernet, wifi;
    CHECK(gs232b_fake_sessions_open(&sessions_fake, GS232B_TRANSPORT_UART1, &uart) == GS232B_OK);
    CHECK(gs232b_fake_sessions_open(&sessions_fake, GS232B_TRANSPORT_ETHERNET_WS, &ethernet) == GS232B_OK);
    CHECK(gs232b_fake_sessions_open(&sessions_fake, GS232B_TRANSPORT_WIFI_WS, &wifi) == GS232B_OK);
    CHECK(uart.session_id != ethernet.session_id && ethernet.session_id != wifi.session_id);
    CHECK(gs232b_session_key_valid(uart) && gs232b_session_key_valid(ethernet) &&
          gs232b_session_key_valid(wifi));

    static const uint8_t uart_line[] = {'R', '\r'};
    static const uint8_t ethernet_line[] = {'C', '\r', '\n'};
    static const uint8_t wifi_line[] = {'W', '0', '1', '0', ' ', '0', '2', '0', '\r'};
    CHECK(gs232b_fake_sessions_feed(&sessions_fake, uart, uart_line, sizeof(uart_line)) == GS232B_OK);
    CHECK(gs232b_fake_sessions_feed(&sessions_fake, ethernet, ethernet_line,
                                    sizeof(ethernet_line)) == GS232B_OK);
    CHECK(gs232b_fake_sessions_feed(&sessions_fake, wifi, wifi_line, sizeof(wifi_line)) == GS232B_OK);

    CHECK(gs232b_fake_sessions_fail_next(&sessions_fake, uart, GS232B_FAKE_SESSION_RECEIVE,
                                         GS232B_ERROR_IO, 1) == GS232B_OK);

    uint8_t buffer[32];
    size_t size;
    CHECK(gs232b_session_receive(&io, uart, buffer, sizeof(buffer), &size) == GS232B_ERROR_IO);
    CHECK(size == 0);
    CHECK(gs232b_session_receive(&io, uart, buffer, 1, &size) == GS232B_OK);
    CHECK(size == 1 && buffer[0] == 'R');
    CHECK(gs232b_session_receive(&io, uart, buffer, sizeof(buffer), &size) == GS232B_OK);
    CHECK(size == 1 && buffer[0] == '\r');
    CHECK(gs232b_session_receive(&io, ethernet, buffer, sizeof(buffer), &size) == GS232B_OK);
    CHECK(size == sizeof(ethernet_line) && memcmp(buffer, ethernet_line, size) == 0);
    CHECK(gs232b_session_receive(&io, wifi, buffer, sizeof(buffer), &size) == GS232B_OK);
    CHECK(size == sizeof(wifi_line) && memcmp(buffer, wifi_line, size) == 0);
    CHECK(gs232b_session_receive(&io, wifi, buffer, sizeof(buffer), &size) ==
          GS232B_ERROR_WOULD_BLOCK);
    CHECK(size == 0);

    static const uint8_t response[] = {'1', '2', '3', '\r', '\n'};
    CHECK(gs232b_session_send(&io, ethernet, response, sizeof(response)) == GS232B_OK);
    CHECK(gs232b_fake_sessions_take_tx(&sessions_fake, ethernet, buffer, sizeof(buffer), &size) ==
          GS232B_OK);
    CHECK(size == sizeof(response) && memcmp(buffer, response, size) == 0);
    CHECK(gs232b_fake_sessions_take_tx(&sessions_fake, uart, buffer, sizeof(buffer), &size) ==
          GS232B_ERROR_WOULD_BLOCK);
    CHECK(size == 0);

    CHECK(gs232b_fake_sessions_fail_next(&sessions_fake, wifi, GS232B_FAKE_SESSION_SEND,
                                         GS232B_ERROR_TIMEOUT, 1) == GS232B_OK);
    CHECK(gs232b_session_send(&io, wifi, response, sizeof(response)) == GS232B_ERROR_TIMEOUT);
    CHECK(gs232b_fake_sessions_take_tx(&sessions_fake, wifi, buffer, sizeof(buffer), &size) ==
          GS232B_ERROR_WOULD_BLOCK);

    gs232b_session_snapshot_t snapshot;
    CHECK(gs232b_fake_sessions_snapshot(&sessions_fake, wifi, &snapshot) == GS232B_OK);
    CHECK(snapshot.transport == GS232B_TRANSPORT_WIFI_WS);
    CHECK(snapshot.last_rx_at_us == 5000);
    CHECK(snapshot.last_error == GS232B_ERROR_TIMEOUT);

    CHECK(gs232b_session_close(&io, uart) == GS232B_OK);
    CHECK(gs232b_fake_sessions_open(&sessions_fake, GS232B_TRANSPORT_UART1, &uart) == GS232B_OK);
    CHECK(uart.session_id == 1 && uart.generation == 2);
    CHECK(gs232b_session_send(&io, (gs232b_session_key_t){1, 1}, response, sizeof(response)) ==
          GS232B_ERROR_STALE_SESSION);
    CHECK(gs232b_fake_sessions_feed(&sessions_fake, (gs232b_session_key_t){1, 1},
                                    response, sizeof(response)) == GS232B_ERROR_STALE_SESSION);
    CHECK(gs232b_fake_sessions_open(&sessions_fake, GS232B_TRANSPORT_UART1, NULL) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    return true;
}

static bool test_sample_failure_clears_value(void)
{
    gs232b_fake_clock_t clock_fake;
    gs232b_fake_clock_init(&clock_fake, 1);
    gs232b_clock_t clock = gs232b_fake_clock_port(&clock_fake);
    gs232b_fake_sampler_t sampler_fake;
    CHECK(gs232b_fake_sampler_init(&sampler_fake, clock) == GS232B_OK);
    gs232b_sampler_t sampler = gs232b_fake_sampler_port(&sampler_fake);
    CHECK(gs232b_fake_sampler_set(&sampler_fake, GS232B_AXIS_AZIMUTH, 10, 300000) == GS232B_OK);
    gs232b_sample_t sample;
    CHECK(gs232b_sampler_read(&sampler, GS232B_AXIS_AZIMUTH, &sample) == GS232B_OK);
    CHECK(sample.valid);

    CHECK(gs232b_fake_fail_next(&sampler_fake.failures[GS232B_AXIS_AZIMUTH],
                                GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs232b_sampler_read(&sampler, GS232B_AXIS_AZIMUTH, &sample) == GS232B_ERROR_IO);
    CHECK(!sample.valid && sample.adc_uv == 0);
    CHECK(gs232b_sampler_read(&sampler, GS232B_AXIS_ELEVATION, &sample) ==
          GS232B_ERROR_NOT_READY);
    CHECK(!sample.valid);
    return true;
}

int main(void)
{
    if (!test_types_and_epoch() || !test_virtual_clock_sampler_and_output() ||
        !test_store_atomic_failure_injection() ||
        !test_multiple_sessions_and_generations() || !test_sample_failure_clears_value()) {
        return 1;
    }
    puts("core contract tests passed");
    return 0;
}
