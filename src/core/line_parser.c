#include "gs232b/line_parser.h"

#include <string.h>

static void reset_line(gs232b_line_parser_t *parser, bool swallow_lf)
{
    parser->length = 0;
    parser->kind = GS232B_LINE_GS;
    parser->discard_error = GS232B_LINE_ERROR_NONE;
    parser->active = false;
    parser->kind_known = false;
    parser->discarding = false;
    parser->error_reported = false;
    parser->swallow_lf = swallow_lf;
}

static void emit_error(gs232b_line_parser_t *parser, gs232b_line_error_t error,
                       gs232b_line_event_fn event_fn, void *event_ctx)
{
    parser->error_reported = true;
    if (event_fn != NULL) {
        const gs232b_line_event_t event = {
            .type = GS232B_LINE_EVENT_ERROR,
            .kind = parser->kind,
            .bytes = NULL,
            .size = 0,
            .error = error
        };
        event_fn(event_ctx, &event);
    }
}

static void discard_line(gs232b_line_parser_t *parser, gs232b_line_error_t error)
{
    if (!parser->discarding) {
        parser->discarding = true;
        parser->discard_error = error;
    }
}

static size_t line_limit(const gs232b_line_parser_t *parser)
{
    return parser->kind == GS232B_LINE_EXTENSION ?
           GS232B_EXTENSION_LINE_MAX_BYTES : GS232B_GS_LINE_MAX_BYTES;
}

static bool bytes_equal(const uint8_t *bytes, size_t size, const char *text)
{
    size_t text_size = strlen(text);
    return size == text_size && memcmp(bytes, text, size) == 0;
}

static bool decimal3_at(const uint8_t *bytes, size_t size, size_t offset)
{
    uint16_t value;
    return offset <= size && size - offset >= 3 &&
           gs232b_byte_span_parse_decimal3(
               (gs232b_byte_span_t){.bytes = bytes + offset, .size = 3}, &value);
}

static bool parse_numeric_tail(const uint8_t *bytes, size_t size, size_t offset,
                               size_t *field_count)
{
    size_t count = 0;
    while (offset < size) {
        if (!decimal3_at(bytes, size, offset)) {
            return false;
        }
        ++count;
        offset += 3;
        if (offset == size) {
            break;
        }
        if (bytes[offset] != ' ') {
            return false;
        }
        do {
            ++offset;
        } while (offset < size && bytes[offset] == ' ');
        if (offset == size) {
            return false;
        }
    }
    *field_count = count;
    return true;
}

static size_t skip_spaces(const uint8_t *bytes, size_t size, size_t offset)
{
    while (offset < size && bytes[offset] == ' ') {
        ++offset;
    }
    return offset;
}

static bool gs_syntax_valid(const uint8_t *bytes, size_t size)
{
    if (size == 0) {
        return true;
    }
    static const char *const exact_commands[] = {
        "R", "L", "A", "U", "D", "E", "S", "T", "C", "C2", "B", "N", "Z",
        "H", "H2", "H3", "O", "O2", "F", "F2", "Y", "YES", "NO"
    };
    for (size_t index = 0; index < sizeof(exact_commands) / sizeof(exact_commands[0]);
         ++index) {
        if (bytes_equal(bytes, size, exact_commands[index])) {
            return true;
        }
    }
    if (bytes_equal(bytes, size, "P36") || bytes_equal(bytes, size, "P45") ||
        (size == 2 && bytes[0] == 'X' && bytes[1] >= '1' && bytes[1] <= '4')) {
        return true;
    }

    if (bytes[0] == 'M') {
        if (size == 1) {
            return true;
        }
        if (!decimal3_at(bytes, size, 1)) {
            return false;
        }
        if (size == 4) {
            return true;
        }
        if (bytes[4] != ' ') {
            return false;
        }
        size_t fields = 0;
        size_t tail = skip_spaces(bytes, size, 5);
        return parse_numeric_tail(bytes, size, tail, &fields) &&
               fields >= 2 && fields <= GS232B_M_SEQUENCE_MAX_POINTS;
    }

    if (bytes[0] == 'W') {
        if (size == 1) {
            return true;
        }
        if (size < 4 || !decimal3_at(bytes, size, 1)) {
            return false;
        }
        if (size == 4) {
            return false;
        }
        if (bytes[4] != ' ') {
            return false;
        }
        size_t fields = 0;
        size_t tail = skip_spaces(bytes, size, 5);
        if (!parse_numeric_tail(bytes, size, tail, &fields)) {
            return false;
        }
        return fields == 1 ||
               (fields >= 4 && fields <= 2 * GS232B_W_SEQUENCE_MAX_PAIRS && fields % 2 == 0);
    }
    return false;
}

static bool extension_byte_valid(uint8_t byte)
{
    return byte >= 0x20;
}

static void finish_line(gs232b_line_parser_t *parser,
                        gs232b_line_event_fn event_fn, void *event_ctx)
{
    if (parser->discarding) {
        if (!parser->error_reported) {
            emit_error(parser, parser->discard_error, event_fn, event_ctx);
        }
    } else if (event_fn != NULL && parser->kind == GS232B_LINE_GS) {
        size_t start = 0;
        size_t end = parser->length;
        while (start < end && parser->storage[start] == ' ') {
            ++start;
        }
        while (end > start && parser->storage[end - 1] == ' ') {
            --end;
        }
        if (!gs_syntax_valid(parser->storage + start, end - start)) {
            emit_error(parser, GS232B_LINE_ERROR_INVALID_SYNTAX, event_fn, event_ctx);
            reset_line(parser, true);
            return;
        }
        const gs232b_line_event_t event = {
            .type = GS232B_LINE_EVENT_COMMIT,
            .kind = parser->kind,
            .bytes = parser->storage + start,
            .size = end - start,
            .error = GS232B_LINE_ERROR_NONE
        };
        event_fn(event_ctx, &event);
    } else if (!parser->discarding && event_fn != NULL) {
        size_t start = 0;
        size_t end = parser->length;
        while (start < end && parser->storage[start] == ' ') {
            ++start;
        }
        while (end > start && parser->storage[end - 1] == ' ') {
            --end;
        }
        const gs232b_line_event_t event = {
            .type = GS232B_LINE_EVENT_COMMIT,
            .kind = parser->kind,
            .bytes = parser->storage + start,
            .size = end - start,
            .error = GS232B_LINE_ERROR_NONE
        };
        event_fn(event_ctx, &event);
    }
    reset_line(parser, true);
}

static void expire_if_needed(gs232b_line_parser_t *parser, gs232b_time_us_t now_us,
                             gs232b_line_event_fn event_fn, void *event_ctx)
{
    if (!parser->active || now_us < parser->last_byte_at_us ||
        now_us - parser->last_byte_at_us < GS232B_LINE_PARTIAL_TIMEOUT_US) {
        return;
    }
    discard_line(parser, GS232B_LINE_ERROR_TIMEOUT);
    if (!parser->error_reported) {
        emit_error(parser, GS232B_LINE_ERROR_TIMEOUT, event_fn, event_ctx);
    }
}

gs232b_error_t gs232b_line_parser_init(gs232b_line_parser_t *parser,
                                       uint8_t *storage, size_t storage_capacity)
{
    if (parser == NULL || storage == NULL ||
        storage_capacity < GS232B_GS_LINE_MAX_BYTES) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(parser, 0, sizeof(*parser));
    parser->storage = storage;
    parser->storage_capacity = storage_capacity;
    parser->kind = GS232B_LINE_GS;
    return GS232B_OK;
}

gs232b_error_t gs232b_line_parser_tick(gs232b_line_parser_t *parser,
                                       gs232b_time_us_t now_us,
                                       gs232b_line_event_fn event_fn, void *event_ctx)
{
    if (parser == NULL || parser->storage == NULL || event_fn == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    expire_if_needed(parser, now_us, event_fn, event_ctx);
    return GS232B_OK;
}

void gs232b_line_parser_invalidate(gs232b_line_parser_t *parser)
{
    if (parser != NULL && parser->active) {
        parser->length = 0;
        parser->discarding = true;
        parser->error_reported = true;
    }
}

gs232b_error_t gs232b_line_parser_feed(gs232b_line_parser_t *parser,
                                       const uint8_t *bytes, size_t size,
                                       gs232b_time_us_t now_us,
                                       gs232b_line_event_fn event_fn, void *event_ctx)
{
    if (parser == NULL || parser->storage == NULL || event_fn == NULL ||
        (size != 0 && bytes == NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < size; ++index) {
        uint8_t byte = bytes[index];
        expire_if_needed(parser, now_us, event_fn, event_ctx);
        if (parser->swallow_lf) {
            parser->swallow_lf = false;
            if (byte == '\n') {
                continue;
            }
        }
        if (byte == '\r') {
            finish_line(parser, event_fn, event_ctx);
            continue;
        }

        parser->active = true;
        parser->last_byte_at_us = now_us;
        if (!parser->kind_known && byte != ' ' && !parser->discarding) {
            parser->kind = byte == '!' ? GS232B_LINE_EXTENSION : GS232B_LINE_GS;
            parser->kind_known = true;
        }
        if (parser->discarding) {
            continue;
        }
        if (parser->length >= line_limit(parser) ||
            parser->length >= parser->storage_capacity) {
            discard_line(parser, GS232B_LINE_ERROR_TOO_LONG);
            continue;
        }

        bool valid;
        if (parser->kind == GS232B_LINE_EXTENSION) {
            valid = extension_byte_valid(byte);
        } else {
            valid = byte >= 0x20 && byte <= 0x7e;
            if (valid && byte >= 'a' && byte <= 'z') {
                byte = (uint8_t)(byte - ('a' - 'A'));
            }
        }
        if (!valid) {
            discard_line(parser, GS232B_LINE_ERROR_INVALID_BYTE);
            continue;
        }
        parser->storage[parser->length++] = byte;
    }
    return GS232B_OK;
}

gs232b_byte_span_t gs232b_byte_span_trim_spaces(gs232b_byte_span_t span)
{
    if (span.bytes == NULL) {
        return (gs232b_byte_span_t){0};
    }
    while (span.size != 0 && span.bytes[0] == ' ') {
        ++span.bytes;
        --span.size;
    }
    while (span.size != 0 && span.bytes[span.size - 1] == ' ') {
        --span.size;
    }
    return span;
}

static uint8_t ascii_upper(uint8_t byte)
{
    return byte >= 'a' && byte <= 'z' ? (uint8_t)(byte - ('a' - 'A')) : byte;
}

bool gs232b_byte_span_equal_ascii_ci(gs232b_byte_span_t span, const char *text)
{
    if (text == NULL || (span.size != 0 && span.bytes == NULL)) {
        return false;
    }
    size_t index = 0;
    for (; index < span.size && text[index] != '\0'; ++index) {
        if (ascii_upper(span.bytes[index]) != ascii_upper((uint8_t)text[index])) {
            return false;
        }
    }
    return index == span.size && text[index] == '\0';
}

bool gs232b_byte_span_next_space_token(gs232b_byte_span_t *remaining,
                                       gs232b_byte_span_t *token)
{
    if (remaining == NULL || token == NULL ||
        (remaining->size != 0 && remaining->bytes == NULL)) {
        return false;
    }
    while (remaining->size != 0 && remaining->bytes[0] == ' ') {
        ++remaining->bytes;
        --remaining->size;
    }
    if (remaining->size == 0) {
        *token = (gs232b_byte_span_t){0};
        return false;
    }
    size_t length = 0;
    while (length < remaining->size && remaining->bytes[length] != ' ') {
        ++length;
    }
    *token = (gs232b_byte_span_t){.bytes = remaining->bytes, .size = length};
    remaining->bytes += length;
    remaining->size -= length;
    return true;
}

bool gs232b_byte_span_parse_decimal3(gs232b_byte_span_t span, uint16_t *value)
{
    if (span.bytes == NULL || span.size != 3 || value == NULL) {
        return false;
    }
    uint16_t parsed = 0;
    for (size_t index = 0; index < span.size; ++index) {
        if (span.bytes[index] < '0' || span.bytes[index] > '9') {
            return false;
        }
        parsed = (uint16_t)(parsed * 10 + (span.bytes[index] - '0'));
    }
    *value = parsed;
    return true;
}
