#include "gs232b/line_parser.h"

#include <string.h>

static void abort_upload(gs232b_line_parser_t *parser)
{
    if (parser->upload_held) {
        parser->upload_held = false;
        parser->upload.abort(parser->upload.ctx);
    }
}

static void reset_line(gs232b_line_parser_t *parser, bool swallow_lf)
{
    parser->length = 0;
    parser->stored = 0;
    parser->pending_spaces = 0;
    parser->kind = GS232B_LINE_GS;
    parser->discard_error = GS232B_LINE_ERROR_NONE;
    parser->upload_error = GS232B_OK;
    parser->active = false;
    parser->kind_known = false;
    parser->discarding = false;
    parser->error_reported = false;
    parser->swallow_lf = swallow_lf;
    parser->is_position = false;
    parser->upload_held = false;
}

static void emit_error(gs232b_line_parser_t *parser, gs232b_line_error_t error,
                       gs232b_line_event_fn event_fn, void *event_ctx)
{
    parser->error_reported = true;
    const gs232b_line_event_t event = {
        .type = GS232B_LINE_EVENT_ERROR,
        .kind = parser->kind,
        .error = error,
        .rejection = parser->upload_error
    };
    event_fn(event_ctx, &event);
}

static void discard_line(gs232b_line_parser_t *parser, gs232b_line_error_t error)
{
    if (!parser->discarding) {
        parser->discarding = true;
        parser->discard_error = error;
        abort_upload(parser);
    }
}

static size_t line_limit(const gs232b_line_parser_t *parser)
{
    return parser->kind == GS232B_LINE_EXTENSION ?
           GS232B_EXTENSION_LINE_MAX_BYTES : GS232B_GS_LINE_MAX_BYTES;
}

static bool gs_syntax_valid(const uint8_t *bytes, size_t size)
{
    if (size == 0) {
        return true;
    }
    static const char *const exact_commands[] = {
        "R", "L", "A", "U", "D", "E", "S", "T", "C", "C2", "B", "N", "Z",
        "H", "H2", "H3", "O", "O2", "F", "F2", "Y", "YES", "NO", "P36", "P45"
    };
    for (size_t i = 0; i < sizeof(exact_commands) / sizeof(exact_commands[0]); ++i) {
        if (size == strlen(exact_commands[i]) &&
            memcmp(bytes, exact_commands[i], size) == 0) {
            return true;
        }
    }
    return size == 2 && bytes[0] == 'X' && bytes[1] >= '1' && bytes[1] <= '4';
}

static void finish_line(gs232b_line_parser_t *parser,
                        gs232b_line_event_fn event_fn, void *event_ctx)
{
    gs232b_position_command_t position;
    if (!parser->discarding && parser->is_position) {
        const gs232b_error_t error = gs232b_position_parser_finish(&parser->position,
                                                                  &position);
        if (error != GS232B_OK) {
            discard_line(parser, error == GS232B_ERROR_RESOURCE_EXHAUSTED ?
                GS232B_LINE_ERROR_TOO_LONG : GS232B_LINE_ERROR_INVALID_SYNTAX);
        } else if (parser->upload_error != GS232B_OK) {
            discard_line(parser, GS232B_LINE_ERROR_UPLOAD_REJECTED);
        }
    } else if (!parser->discarding && parser->kind == GS232B_LINE_GS &&
               !gs_syntax_valid(parser->storage, parser->stored)) {
        discard_line(parser, GS232B_LINE_ERROR_INVALID_SYNTAX);
    }
    if (parser->discarding) {
        if (!parser->error_reported) {
            emit_error(parser, parser->discard_error, event_fn, event_ctx);
        }
    } else {
        size_t end = parser->stored;
        while (end > 0 && parser->storage[end - 1] == ' ') {
            --end;
        }
        const gs232b_line_event_t event = {
            .type = GS232B_LINE_EVENT_COMMIT,
            .kind = parser->kind,
            .bytes = parser->is_position ? NULL : parser->storage,
            .size = parser->is_position ? 0 : end,
            .position = parser->is_position ? &position : NULL
        };
        /* NOTE: 成功 CR 的预约转交事件接收方，不能在 reset 时释放。 */
        parser->upload_held = false;
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
                                       uint8_t *storage, size_t storage_capacity,
                                       gs232b_line_upload_t upload)
{
    if (parser == NULL || storage == NULL ||
        storage_capacity < GS232B_LINE_STORAGE_BYTES ||
        upload.begin == NULL || upload.abort == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(parser, 0, sizeof(*parser));
    parser->storage = storage;
    parser->storage_capacity = storage_capacity;
    parser->upload = upload;
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
        abort_upload(parser);
        parser->stored = 0;
        parser->discarding = true;
        parser->error_reported = true;
    }
}

static void store_byte(gs232b_line_parser_t *parser, uint8_t byte)
{
    if (!parser->kind_known) {
        return;
    }
    if (parser->is_position) {
        const gs232b_error_t error = gs232b_position_parser_push(&parser->position, byte);
        if (error != GS232B_OK) {
            discard_line(parser, error == GS232B_ERROR_RESOURCE_EXHAUSTED ?
                GS232B_LINE_ERROR_TOO_LONG : GS232B_LINE_ERROR_INVALID_SYNTAX);
        }
        return;
    }
    if (parser->kind == GS232B_LINE_GS && byte == ' ') {
        ++parser->pending_spaces;
        return;
    }
    if (parser->pending_spaces >= parser->storage_capacity - parser->stored) {
        discard_line(parser, GS232B_LINE_ERROR_INVALID_SYNTAX);
        return;
    }
    memset(parser->storage + parser->stored, ' ', parser->pending_spaces);
    parser->stored += parser->pending_spaces;
    parser->pending_spaces = 0;
    if (parser->stored >= parser->storage_capacity) {
        discard_line(parser, GS232B_LINE_ERROR_TOO_LONG);
        return;
    }
    parser->storage[parser->stored++] = byte;
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
    for (size_t i = 0; i < size; ++i) {
        uint8_t byte = bytes[i];
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
        if (parser->discarding) {
            continue;
        }
        const bool first = !parser->kind_known && byte != ' ';
        if (first) {
            parser->kind = byte == '!' ? GS232B_LINE_EXTENSION : GS232B_LINE_GS;
            parser->kind_known = true;
        }
        if (parser->length >= line_limit(parser)) {
            discard_line(parser, GS232B_LINE_ERROR_TOO_LONG);
            continue;
        }
        ++parser->length;
        const bool valid = parser->kind == GS232B_LINE_EXTENSION ? byte >= 0x20 :
                                                            byte >= 0x20 && byte <= 0x7e;
        if (!valid) {
            discard_line(parser, GS232B_LINE_ERROR_INVALID_BYTE);
            continue;
        }
        if (parser->kind == GS232B_LINE_GS && byte >= 'a' && byte <= 'z') {
            byte = (uint8_t)(byte - ('a' - 'A'));
        }
        if (first && (byte == 'M' || byte == 'W')) {
            parser->is_position = true;
            uint16_t *angles = NULL;
            parser->upload_error = parser->upload.begin(parser->upload.ctx, &angles);
            parser->upload_held = parser->upload_error == GS232B_OK;
            if (parser->upload_held && angles == NULL) {
                abort_upload(parser);
                parser->upload_error = GS232B_ERROR_NOT_READY;
            }
            gs232b_position_parser_init(&parser->position, (char)byte,
                                        parser->upload_held ? angles : NULL);
        } else {
            store_byte(parser, byte);
        }
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
