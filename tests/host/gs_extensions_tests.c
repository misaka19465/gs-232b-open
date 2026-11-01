#include "gs_test_support.h"

typedef struct {
    unsigned calls;
    gs232b_error_t check_error;
    gs232b_session_key_t last_session;
    gs232b_transport_t last_transport;
    uint8_t decoded[2048];
    size_t decoded_size;
} extension_state_t;

static gs232b_error_t check(void *ctx, const gs232b_extension_request_t *request)
{
    (void)request;
    return ((extension_state_t *)ctx)->check_error;
}

static gs232b_error_t echo(void *ctx, const gs232b_extension_request_t *request,
    uint8_t *data, size_t capacity, size_t *size)
{
    extension_state_t *state = ctx;
    gs232b_json_value_t value;
    gs232b_error_t error = gs232b_json_get(request->arguments, "message", &value);
    if (error != GS232B_OK) return error;
    error = gs232b_json_string_decode(value, state->decoded, sizeof(state->decoded), &state->decoded_size);
    if (error != GS232B_OK) return error;
    const char prefix[] = "{\"message\":";
    size_t head = sizeof(prefix) - 1U, n;
    if (capacity < head + 1U) return GS232B_ERROR_BUFFER_TOO_SMALL;
    error = gs232b_json_string_encode((gs232b_byte_span_t){state->decoded, state->decoded_size},
        data + head, capacity - head - 1U, &n);
    if (error != GS232B_OK) return error;
    memcpy(data, prefix, head); data[head + n] = '}';
    *size = head + n + 1U;
    ++state->calls;
    state->last_session = request->context.session;
    state->last_transport = request->transport;
    return GS232B_OK;
}

static gs232b_error_t ping(void *ctx, const gs232b_extension_request_t *request,
    uint8_t *data, size_t capacity, size_t *size)
{
    extension_state_t *state = ctx;
    (void)data; (void)capacity;
    ++state->calls;
    state->last_session = request->context.session;
    state->last_transport = request->transport;
    *size = 0;
    return GS232B_OK;
}

static bool setup(gs_fixture_t *fixture, extension_state_t *state)
{
    CHECK(gs_init(fixture, true));
    memset(state, 0, sizeof(*state));
    static const gs232b_json_field_t fields[] = {
        {.name = "message", .type = GS232B_JSON_STRING, .required = true,
         .max_bytes = GS232B_EXTENSION_DATA_MAX_BYTES}
    };
    static const gs232b_json_schema_t schema = {fields, 1};
    const gs232b_extension_handler_t handlers[] = {
        {.domain = "TEST", .action = "ECHO", .schema = &schema, .check = check, .handle = echo, .ctx = state},
        {.domain = "TEST", .action = "PING", .check = check, .handle = ping, .ctx = state}
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        CHECK(gs232b_dispatcher_register_extension(&fixture->dispatcher, &handlers[i]) == GS232B_OK);
    }
    return true;
}

static gs232b_error_t feed_chunks(gs_fixture_t *fixture, size_t session,
    const uint8_t *bytes, size_t size, size_t chunk)
{
    gs232b_error_t first = GS232B_OK;
    gs232b_command_context_t context = gs_context(fixture, session);
    for (size_t i = 0; i < size;) {
        size_t count = size - i < chunk ? size - i : chunk;
        gs232b_error_t error = gs232b_dispatcher_feed(&fixture->dispatcher, context, bytes + i, count);
        if (first == GS232B_OK) first = error;
        i += count;
    }
    return first;
}

static bool test_utf8_and_response_isolation(void)
{
    gs_fixture_t f; extension_state_t state;
    CHECK(setup(&f, &state));
    static const char line[] = "  !teSt   eCho {\"message\":\"中文\\\"\\\\\\u4e16\\uD834\\uDD1E\\n\\u0000\"}  \r\n";
    CHECK(feed_chunks(&f, 1, (const uint8_t *)line, sizeof(line) - 1U, 1) == GS232B_OK);
    CHECK(state.calls == 0);
    CHECK(gs_expect(&f, 1, ""));
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(gs_expect(&f, 1, "!{\"ok\":true,\"cmd\":\"TEST ECHO\",\"data\":{\"message\":\"中文\\\"\\\\世𝄞\\u000a\\u0000\"}}\r\n"));
    CHECK(state.calls == 1 && gs232b_session_key_equal(state.last_session, f.sessions[1]));
    CHECK(state.last_transport == GS232B_TRANSPORT_ETHERNET_WS);
    CHECK(state.decoded_size != 0 && state.decoded[state.decoded_size - 1U] == 0);
    CHECK(!gs232b_session_key_valid(f.dispatcher.control.owner));
    CHECK(gs_feed(&f, 0, "C\r!TEST PING\r!UNKNOWN RUN\rB\r@\r") == GS232B_ERROR_NOT_FOUND);
    CHECK(gs_expect(&f, 0, ""));
    CHECK(gs_drain(&f));
    CHECK(gs_expect(&f, 0, "AZ=090\r\n!{\"ok\":true,\"cmd\":\"TEST PING\",\"data\":{}}\r\n"
        "!{\"ok\":false,\"cmd\":\"UNKNOWN RUN\",\"error\":\"NOT_FOUND\"}\r\nEL=090\r\n?>\r\n"));
    CHECK(state.calls == 2);
    CHECK(gs_run(&f, 2, "!TEST PING {}\r", "!{\"ok\":true,\"cmd\":\"TEST PING\",\"data\":{}}\r\n"));
    CHECK(gs_quiet(&f));
    CHECK(sizeof(gs232b_response_t) < 128);
    return true;
}

static bool test_rejections_have_no_side_effects(void)
{
    gs_fixture_t f; extension_state_t state;
    CHECK(setup(&f, &state));
    CHECK(gs_run(&f, 0, "R\r", "\r"));
    gs232b_motion_snapshot_t before, after;
    CHECK(gs232b_motion_snapshot(&f.sim.motion, &before) == GS232B_OK);
    static const char *const invalid[] = {
        "!TEST ECHO {\"message\":}\r", "!TEST ECHO []\r", "!TEST ECHO {\"message\":\"x\"} trailing\r",
        "!TEST ECHO {\"message\":1}\r", "!TEST ECHO {\"message\":\"x\",\"illegal\":true}\r",
        "!TEST ECHO {\"message\":\"x\",\"\\u006dessage\":\"y\"}\r",
        "!TEST ECHO {\"message\":\"\\uD800\"}\r", "!TEST ECHO {}\r",
        "!TEST ECHO {\"message\":\"\300\257\"}\r", "!TEST PING {\"illegal\":1}\r"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(gs_feed(&f, 1, invalid[i]) == GS232B_ERROR_INVALID_ARGUMENT);
        const char *cmd = i == 9 ? "TEST PING" : "TEST ECHO";
        char expected[128]; snprintf(expected, sizeof(expected), "!{\"ok\":false,\"cmd\":\"%s\",\"error\":\"INVALID_ARGUMENT\"}\r\n", cmd);
        CHECK(gs_expect(&f, 1, expected));
        CHECK(state.calls == 0 && f.dispatcher.control.request_count == 0);
        CHECK(gs232b_session_key_equal(f.dispatcher.control.owner, f.sessions[0]));
    }
    static const uint8_t control[] = "!TEST ECHO {\"message\":\"x\0y\"}\r";
    CHECK(feed_chunks(&f, 1, control, sizeof(control) - 1U, 7) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs_expect(&f, 1, "!{\"ok\":false,\"cmd\":\"\",\"error\":\"INVALID_ARGUMENT\"}\r\n"));
    CHECK(gs232b_motion_snapshot(&f.sim.motion, &after) == GS232B_OK);
    CHECK(memcmp(&before, &after, sizeof(before)) == 0);
    for (size_t i = 0; i < GS232B_STORE_DOMAIN_COUNT; ++i) CHECK(!f.store.records[i].present);
    state.check_error = GS232B_ERROR_NOT_AUTHORIZED;
    CHECK(gs_feed(&f, 1, "!TEST PING\r") == GS232B_ERROR_NOT_AUTHORIZED);
    CHECK(gs_expect(&f, 1, "!{\"ok\":false,\"cmd\":\"TEST PING\",\"error\":\"NOT_AUTHORIZED\"}\r\n"));
    CHECK(state.calls == 0);
    CHECK(gs_run(&f, 0, "S\r", "\r"));
    return true;
}

static bool test_boundaries_and_recovery(void)
{
    gs_fixture_t f; extension_state_t state;
    CHECK(setup(&f, &state));
    uint8_t line[GS232B_EXTENSION_LINE_MAX_BYTES + 8U];
    const char prefix[] = "!TEST ECHO {\"message\":\"";
    size_t head = sizeof(prefix) - 1U;
    memcpy(line, prefix, head);
    memset(line + head, 'x', GS232B_EXTENSION_LINE_MAX_BYTES - head - 2U);
    memcpy(line + GS232B_EXTENSION_LINE_MAX_BYTES - 2U, "\"}", 2);
    line[GS232B_EXTENSION_LINE_MAX_BYTES] = '\r';
    CHECK(feed_chunks(&f, 0, line, GS232B_EXTENSION_LINE_MAX_BYTES, 73) == GS232B_OK);
    CHECK(gs_expect(&f, 0, "") && state.calls == 0);
    CHECK(feed_chunks(&f, 0, line + GS232B_EXTENSION_LINE_MAX_BYTES, 1, 1) == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK && state.calls == 1);
    uint8_t bytes[4096]; size_t size;
    CHECK(gs232b_fake_sessions_take_tx(&f.io, f.sessions[0], bytes, sizeof(bytes), &size) == GS232B_OK);
    CHECK(size > GS232B_EXTENSION_LINE_MAX_BYTES && bytes[0] == '!' && bytes[size - 2] == '\r' && bytes[size - 1] == '\n');
    gs232b_json_value_t response;
    CHECK(gs232b_json_parse((gs232b_byte_span_t){bytes + 1U, size - 3U}, &response) == GS232B_OK);
    line[GS232B_EXTENSION_LINE_MAX_BYTES] = ' ';
    line[GS232B_EXTENSION_LINE_MAX_BYTES + 1U] = '\r';
    CHECK(feed_chunks(&f, 1, line, GS232B_EXTENSION_LINE_MAX_BYTES + 2U, 256) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(gs_expect(&f, 1, "!{\"ok\":false,\"cmd\":\"\",\"error\":\"RESOURCE_EXHAUSTED\"}\r\n"));
    CHECK(state.calls == 1);
    CHECK(gs_run(&f, 1, "!TEST PING\r\n", "!{\"ok\":true,\"cmd\":\"TEST PING\",\"data\":{}}\r\n"));
    CHECK(gs_feed(&f, 2, "!TEST ECHO {\"message\":\"partial") == GS232B_OK);
    f.sim.now_us += GS232B_LINE_PARTIAL_TIMEOUT_US;
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(gs_expect(&f, 2, "!{\"ok\":false,\"cmd\":\"\",\"error\":\"TIMEOUT\"}\r\n"));
    CHECK(gs_feed(&f, 2, "\"}\r\n") == GS232B_OK);
    CHECK(gs_expect(&f, 2, ""));
    CHECK(gs_run(&f, 2, "C\r", "AZ=090\r\n"));
    CHECK(gs_quiet(&f));
    return true;
}

static bool test_cancellation_execution_recheck_and_input_loss(void)
{
    gs_fixture_t f; extension_state_t state;
    CHECK(setup(&f, &state));
    CHECK(gs_feed(&f, 0, "!TEST PING\rS\r!TEST PING\r") == GS232B_OK);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(state.calls == 0);
    CHECK(gs_expect(&f, 0, "!{\"ok\":false,\"cmd\":\"TEST PING\",\"error\":\"CONFLICT\"}\r\n\r"
        "!{\"ok\":false,\"cmd\":\"TEST PING\",\"error\":\"CONFLICT\"}\r\n"));
    CHECK(gs_feed(&f, 1, "!TEST PING\r") == GS232B_OK);
    state.check_error = GS232B_ERROR_BUSY;
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_ERROR_BUSY);
    CHECK(gs_expect(&f, 1, "!{\"ok\":false,\"cmd\":\"TEST PING\",\"error\":\"BUSY\"}\r\n"));
    CHECK(state.calls == 0);
    state.check_error = GS232B_OK;
    CHECK(gs_feed(&f, 2, "!TEST ECHO {\"message\":\"half") == GS232B_OK);
    CHECK(gs232b_dispatcher_input_lost(&f.dispatcher, f.sessions[2]) == GS232B_OK);
    CHECK(gs_expect(&f, 2, "!{\"ok\":false,\"cmd\":\"\",\"error\":\"IO\"}\r\n"));
    CHECK(gs_feed(&f, 2, "\"}\r") == GS232B_OK);
    CHECK(gs_expect(&f, 2, ""));
    CHECK(gs_run(&f, 2, "!TEST PING\r", "!{\"ok\":true,\"cmd\":\"TEST PING\",\"data\":{}}\r\n"));
    CHECK(gs_quiet(&f));
    return true;
}

int main(void)
{
    if (!test_utf8_and_response_isolation() || !test_rejections_have_no_side_effects() ||
        !test_boundaries_and_recovery() || !test_cancellation_execution_recheck_and_input_loss()) return EXIT_FAILURE;
    puts("Extension dispatcher boundaries, isolation, exact responses and side-effect fencing passed");
    return EXIT_SUCCESS;
}
