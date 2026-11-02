#ifndef GS232B_MANAGEMENT_TEST_SUPPORT_H
#define GS232B_MANAGEMENT_TEST_SUPPORT_H

#include "network_test_support.h"

static inline bool management_reply(gs_fixture_t *fixture, size_t session,
    const char *line, gs232b_error_t expected, char bytes[GS232B_EXTENSION_RESPONSE_MAX_BYTES + 1U])
{
    const gs232b_error_t admitted = gs_feed(fixture, session, line);
    if (admitted == GS232B_OK) (void)gs232b_dispatcher_tick(&fixture->dispatcher);
    else CHECK(admitted == expected);
    size_t size = 0;
    CHECK(gs232b_fake_sessions_take_tx(&fixture->io, fixture->sessions[session],
        (uint8_t *)bytes, GS232B_EXTENSION_RESPONSE_MAX_BYTES, &size) == GS232B_OK);
    CHECK(size >= 4 && bytes[0] == '!' && bytes[size - 2U] == '\r' && bytes[size - 1U] == '\n');
    bytes[size] = '\0';
    gs232b_json_value_t response, result;
    CHECK(gs232b_json_parse((gs232b_byte_span_t){(uint8_t *)bytes + 1, size - 3U}, &response) == GS232B_OK);
    CHECK(gs232b_json_get(response, "ok", &result) == GS232B_OK);
    CHECK(result.type == GS232B_JSON_BOOL);
    if (result.raw.size != (expected == GS232B_OK ? 4U : 5U)) {
        gs232b_json_value_t command, error;
        CHECK(gs232b_json_get(response, "cmd", &command) == GS232B_OK);
        if (gs232b_json_get(response, "error", &error) == GS232B_OK) {
            fprintf(stderr, "command %.*s returned %.*s\n", (int)command.raw.size,
                command.raw.bytes, (int)error.raw.size, error.raw.bytes);
        }
        CHECK(false);
    }
    if (expected != GS232B_OK) {
        CHECK(gs232b_json_get(response, "error", &result) == GS232B_OK);
        uint8_t name[64]; size_t length = 0;
        CHECK(gs232b_json_string_decode(result, name, sizeof(name), &length) == GS232B_OK);
        if (length != strlen(gs232b_error_name(expected)) ||
            memcmp(name, gs232b_error_name(expected), length) != 0) {
            fprintf(stderr, "unexpected management error for session %zu: %.*s\n", session, (int)length, name);
            CHECK(false);
        }
    }
    return true;
}

static inline bool management_command(gs_fixture_t *fixture, size_t session,
    const char *line, gs232b_error_t expected, const char *contains)
{
    char bytes[GS232B_EXTENSION_RESPONSE_MAX_BYTES + 1U];
    CHECK(management_reply(fixture, session, line, expected, bytes));
    CHECK(contains == NULL || strstr(bytes, contains) != NULL);
    return true;
}

static inline bool management_enable(gs_fixture_t *fixture, char token[65])
{
    char bytes[GS232B_EXTENSION_RESPONSE_MAX_BYTES + 1U];
    CHECK(management_reply(fixture, 0, "!ADMIN ENABLE\r", GS232B_OK, bytes));
    gs232b_json_value_t response, data, value;
    CHECK(gs232b_json_parse((gs232b_byte_span_t){(uint8_t *)bytes + 1, strlen(bytes) - 3U}, &response) == GS232B_OK);
    CHECK(gs232b_json_get(response, "data", &data) == GS232B_OK);
    CHECK(gs232b_json_get(data, "token", &value) == GS232B_OK);
    size_t size = 0;
    CHECK(gs232b_json_string_decode(value, (uint8_t *)token, 64, &size) == GS232B_OK && size == 64);
    token[size] = '\0';
    return true;
}

static inline bool gs_reopen_management(gs_fixture_t *fixture, size_t index,
    gs232b_transport_t transport)
{
    const gs232b_session_key_t old = fixture->sessions[index];
    CHECK(gs232b_dispatcher_session_close(&fixture->dispatcher, old) == GS232B_OK);
    CHECK(gs232b_fake_sessions_open(&fixture->io, transport, &fixture->sessions[index]) == GS232B_OK);
    CHECK(gs232b_dispatcher_management_session_open(&fixture->dispatcher, transport,
        fixture->lines[index], sizeof(fixture->lines[index]), &fixture->sessions[index]) == GS232B_OK);
    return gs_expect(fixture, index, "Connect OK\r\n");
}

static inline bool management_auth(gs_fixture_t *fixture, size_t session, const char *token,
    gs232b_error_t expected)
{
    char command[128];
    snprintf(command, sizeof(command), "!SESSION AUTH {\"token\":\"%s\"}\r", token);
    return management_command(fixture, session, command, expected, NULL);
}

#endif
