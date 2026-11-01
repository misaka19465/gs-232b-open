#include "gs232b/extension.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return false; } } while (0)
#define SPAN(s) ((gs232b_byte_span_t){(const uint8_t *)(s), strlen(s)})

typedef struct { unsigned calls; gs232b_error_t result; bool bad_output; } state_t;

static gs232b_error_t handle(void *ctx, const gs232b_extension_request_t *request,
    uint8_t *data, size_t capacity, size_t *size)
{
    state_t *state = ctx;
    (void)request;
    ++state->calls;
    const char *output = state->bad_output ? "?>\r\n" : "{\"message\":\"中文\\\"\\\\\\u4e16\"}";
    *size = strlen(output);
    if (capacity < *size) return GS232B_ERROR_BUFFER_TOO_SMALL;
    memcpy(data, output, *size);
    return state->result;
}

static bool test_json_and_strings(void)
{
    static const char *const valid[] = {
        "{}", " {\"text\":\"中文\\\"\\\\\\u4e16\\n\\r\\t\\b\\f\\/\\u0000\\uD834\\uDD1E\"} ",
        "{\"a\":[true,false,null,0,-0,123,-12,0.5,1e2,1E-2,1e+9,{},[]]}",
        "{\"a\":1,\"aa\":2}", "{\"\":\"\"}", "{\"中\":\"😀\"}"
    };
    static const char *const invalid[] = {
        "", "{", "{}{}", "{\"a\":}", "{\"a\":1,}", "{\"a\" 1}", "{a:1}",
        "{\"a\":01}", "{\"a\":-01}", "{\"a\":+1}", "{\"a\":NaN}", "{\"a\":Infinity}",
        "{\"a\":.5}", "{\"a\":1.}", "{\"a\":1e}", "{\"a\":1e+}", "[1,]",
        "{\"a\":\"\\x01\"}", "{\"a\":\"\\uD800\"}", "{\"a\":\"\\uDC00\"}",
        "{\"a\":\"\\uD800\\u0041\"}", "{\"a\":\"\\uXYZ0\"}", "{\"a\":\"\\u123\"}",
        "{\"a\":1,\"a\":2}", "{\"a\":1,\"\\u0061\":2}",
        "{\"inner\":{\"x\":1,\"\\u0078\":2}}", "{\"a\":true false}",
        "{\n\"a\":1}", "{\"a\":\"\t\"}", "{\"a\":\"\177\"}",
        "{\"a\":\"\300\257\"}", "{\"a\":\"\355\240\200\"}", "{\"a\":\"\364\220\200\200\"}"
    };
    gs232b_json_value_t value;
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        CHECK(gs232b_json_parse(SPAN(valid[i]), &value) == GS232B_OK);
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(gs232b_json_parse(SPAN(invalid[i]), &value) == GS232B_ERROR_INVALID_ARGUMENT);
    }
    static const char encoded[] = "\"中文\\\"\\\\\\u0000\\uD834\\uDD1E\\n\"";
    static const uint8_t decoded[] = {'\xe4','\xb8','\xad','\xe6','\x96','\x87','"','\\',0,0xf0,0x9d,0x84,0x9e,'\n'};
    CHECK(gs232b_json_parse(SPAN(encoded), &value) == GS232B_OK);
    uint8_t out[128]; memset(out, 0xa5, sizeof(out)); size_t n;
    CHECK(gs232b_json_string_decode(value, NULL, 0, &n) == GS232B_ERROR_BUFFER_TOO_SMALL);
    CHECK(n == sizeof(decoded));
    CHECK(gs232b_json_string_decode(value, out, n - 1U, &n) == GS232B_ERROR_BUFFER_TOO_SMALL);
    CHECK(out[0] == 0xa5);
    CHECK(gs232b_json_string_decode(value, out, sizeof(out), &n) == GS232B_OK);
    CHECK(n == sizeof(decoded) && memcmp(out, decoded, n) == 0 && out[n] == 0xa5);
    uint8_t json[128];
    CHECK(gs232b_json_string_encode((gs232b_byte_span_t){out, n}, json, sizeof(json), &n) == GS232B_OK);
    CHECK(gs232b_json_parse((gs232b_byte_span_t){json, n}, &value) == GS232B_OK);
    CHECK(gs232b_json_string_decode(value, out, sizeof(out), &n) == GS232B_OK);
    CHECK(n == sizeof(decoded) && memcmp(out, decoded, n) == 0);
    memset(json, 0xa5, sizeof(json));
    CHECK(gs232b_json_string_encode(SPAN("\"\\\n"), json, 1, &n) == GS232B_ERROR_BUFFER_TOO_SMALL);
    CHECK(json[0] == 0xa5 && n == 12);
    CHECK(gs232b_json_parse(SPAN("4294967295"), &value) == GS232B_OK);
    uint32_t number;
    CHECK(gs232b_json_uint32(value, &number) == GS232B_OK && number == UINT32_MAX);
    CHECK(gs232b_json_parse(SPAN("4294967296"), &value) == GS232B_OK);
    CHECK(gs232b_json_uint32(value, &number) == GS232B_ERROR_INVALID_ARGUMENT);
    char nested[128]; size_t index = 0;
    for (unsigned i = 0; i < GS232B_JSON_MAX_DEPTH + 2U; ++i) nested[index++] = '[';
    nested[index++] = '0';
    for (unsigned i = 0; i < GS232B_JSON_MAX_DEPTH + 2U; ++i) nested[index++] = ']';
    CHECK(gs232b_json_parse((gs232b_byte_span_t){(uint8_t *)nested, index}, &value) == GS232B_ERROR_INVALID_ARGUMENT);
    return true;
}

static bool test_schema(void)
{
    static const gs232b_json_field_t child_fields[] = {
        {.name = "enabled", .type = GS232B_JSON_BOOL, .required = true}
    };
    static const gs232b_json_schema_t child = {child_fields, 1};
    static const gs232b_json_field_t fields[] = {
        {.name = "message", .type = GS232B_JSON_STRING, .required = true, .max_bytes = 3},
        {.name = "config", .type = GS232B_JSON_OBJECT, .object = &child},
        {.name = "count", .type = GS232B_JSON_NUMBER}
    };
    static const gs232b_json_schema_t schema = {fields, 3};
    static const char *const valid[] = {"{\"message\":\"中\"}", "{\"message\":\"\\u4e2d\",\"config\":{\"enabled\":true}}",
        "{\"\\u006dessage\":\"a\",\"count\":1}"};
    static const char *const invalid[] = {"{}", "{\"message\":\"中文\"}", "{\"message\":1}",
        "{\"message\":\"x\",\"extra\":0}", "{\"message\":\"x\",\"config\":{}}",
        "{\"message\":\"x\",\"config\":{\"enabled\":true,\"extra\":1}}", "{\"message\":\"x\",\"count\":false}"};
    gs232b_json_value_t value, field;
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        CHECK(gs232b_json_parse(SPAN(valid[i]), &value) == GS232B_OK);
        CHECK(gs232b_json_validate_object(value, &schema) == GS232B_OK);
        CHECK(gs232b_json_get(value, "message", &field) == GS232B_OK && field.type == GS232B_JSON_STRING);
        CHECK(gs232b_json_get(value, "missing", &field) == GS232B_ERROR_NOT_FOUND);
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(gs232b_json_parse(SPAN(invalid[i]), &value) == GS232B_OK);
        CHECK(gs232b_json_validate_object(value, &schema) == GS232B_ERROR_INVALID_ARGUMENT);
    }
    CHECK(gs232b_json_parse(SPAN("{\"x\":1}"), &value) == GS232B_OK);
    CHECK(gs232b_json_validate_object(value, NULL) == GS232B_ERROR_INVALID_ARGUMENT);
    return true;
}

static bool test_registry_and_encoding(void)
{
    static const gs232b_json_field_t fields[] = {{.name = "x", .type = GS232B_JSON_STRING}};
    static const gs232b_json_schema_t schema = {fields, 1};
    gs232b_extension_registry_t registry;
    gs232b_extension_registry_init(&registry);
    state_t state = {0};
    gs232b_extension_handler_t descriptor = {
        .domain = "demo", .action = "run", .schema = &schema, .handle = handle, .ctx = &state
    };
    CHECK(gs232b_extension_register(&registry, &descriptor) == GS232B_OK);
    CHECK(gs232b_extension_register(&registry, &descriptor) == GS232B_ERROR_CONFLICT);
    descriptor.domain = "bad name";
    CHECK(gs232b_extension_register(&registry, &descriptor) == GS232B_ERROR_INVALID_ARGUMENT);
    gs232b_extension_request_t request;
    const char *line = "  !DeMo   rUn {\"x\":\"中文\\\"\\\\\\u4e16\"} ";
    CHECK(gs232b_extension_parse((const uint8_t *)line, strlen(line), &request) == GS232B_OK);
    CHECK(strcmp(request.cmd, "DEMO RUN") == 0 && request.has_json);
    uint8_t data[2048], bytes[2304];
    gs232b_extension_response_t response; size_t n;
    CHECK(gs232b_extension_dispatch(&registry, &request, data, sizeof(data), &response) == GS232B_OK);
    CHECK(state.calls == 1);
    CHECK(gs232b_extension_response_encode(&response, bytes, sizeof(bytes), &n) == GS232B_OK);
    const char *expected = "!{\"ok\":true,\"cmd\":\"DEMO RUN\",\"data\":{\"message\":\"中文\\\"\\\\\\u4e16\"}}\r\n";
    CHECK(n == strlen(expected) && memcmp(bytes, expected, n) == 0);
    memset(bytes, 0xa5, sizeof(bytes));
    CHECK(gs232b_extension_response_encode(&response, bytes, 1, &n) == GS232B_ERROR_BUFFER_TOO_SMALL);
    CHECK(bytes[0] == 0xa5 && n == strlen(expected));
    for (int e = GS232B_ERROR_INVALID_ARGUMENT; e <= GS232B_ERROR_CORRUPT; ++e) {
        state.result = (gs232b_error_t)e;
        CHECK(gs232b_extension_dispatch(&registry, &request, data, sizeof(data), &response) == (gs232b_error_t)e);
        CHECK(gs232b_extension_response_encode(&response, bytes, sizeof(bytes), &n) == GS232B_OK);
        char error[128]; snprintf(error, sizeof(error), "!{\"ok\":false,\"cmd\":\"DEMO RUN\",\"error\":\"%s\"}\r\n", gs232b_error_name((gs232b_error_t)e));
        CHECK(n == strlen(error) && memcmp(bytes, error, n) == 0);
    }
    state.result = GS232B_OK; state.bad_output = true;
    CHECK(gs232b_extension_dispatch(&registry, &request, data, sizeof(data), &response) == GS232B_ERROR_CORRUPT);
    static const char *const invalid[] = {"!", "!DEMO", "! DEMO RUN", "!DEMO RUN{}", "!DEMO RUN []",
        "!DEMO RUN {} extra", "!DEMO RUN {\"x\":}", "!DEMO RUN {\"x\":1,\"x\":2}", "!DEMO RUN\t{}"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(gs232b_extension_parse((const uint8_t *)invalid[i], strlen(invalid[i]), &request) == GS232B_ERROR_INVALID_ARGUMENT);
    }
    line = "!DEMO RUN {\"illegal\":true}";
    CHECK(gs232b_extension_parse((const uint8_t *)line, strlen(line), &request) == GS232B_OK);
    unsigned calls = state.calls;
    CHECK(gs232b_extension_dispatch(&registry, &request, data, sizeof(data), &response) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(state.calls == calls);
    char action[16];
    descriptor.domain = "DEMO";
    for (unsigned i = 1; i < GS232B_EXTENSION_MAX_HANDLERS; ++i) {
        snprintf(action, sizeof(action), "A%u", i); descriptor.action = action;
        CHECK(gs232b_extension_register(&registry, &descriptor) == GS232B_OK);
    }
    descriptor.action = "FULL";
    CHECK(gs232b_extension_register(&registry, &descriptor) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    return true;
}

int main(void)
{
    if (!test_json_and_strings() || !test_schema() || !test_registry_and_encoding()) return 1;
    puts("Strict extension JSON, Unicode, schemas, registry and single-line encoding passed");
    return 0;
}
