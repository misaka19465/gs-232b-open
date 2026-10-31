#include "gs232b/line_parser.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

#define COLLECTOR_EVENT_CAPACITY 16
#define COLLECTOR_LINE_CAPACITY 128

typedef struct {
    gs232b_line_event_type_t type;
    gs232b_line_kind_t kind;
    gs232b_line_error_t error;
    size_t size;
    uint8_t bytes[COLLECTOR_LINE_CAPACITY];
    bool is_position;
    gs232b_position_command_t position;
    uint16_t angles[6];
} collected_event_t;

typedef struct {
    collected_event_t events[COLLECTOR_EVENT_CAPACITY];
    size_t count;
    uint16_t staging[GS232B_POSITION_MAX_ANGLES];
} collector_t;

static void collect_event(void *ctx, const gs232b_line_event_t *event)
{
    collector_t *collector = ctx;
    if (collector->count >= COLLECTOR_EVENT_CAPACITY) {
        return;
    }
    collected_event_t *copy = &collector->events[collector->count++];
    copy->type = event->type;
    copy->kind = event->kind;
    copy->error = event->error;
    copy->size = event->size;
    copy->is_position = event->position != NULL;
    if (event->position != NULL) {
        copy->position = *event->position;
        size_t angles = copy->position.point_count *
                        (copy->position.command == 'W' ? 2U : 1U);
        if (angles <= sizeof(copy->angles) / sizeof(copy->angles[0])) {
            memcpy(copy->angles, copy->position.angles, angles * sizeof(uint16_t));
        }
    }
    if (event->type == GS232B_LINE_EVENT_COMMIT && event->size <= sizeof(copy->bytes)) {
        memcpy(copy->bytes, event->bytes, event->size);
    }
}

static gs232b_error_t begin_upload(void *ctx, uint16_t **angles)
{
    *angles = ((collector_t *)ctx)->staging;
    return GS232B_OK;
}

static void abort_upload(void *ctx)
{
    (void)ctx;
}

static bool parser_init(gs232b_line_parser_t *parser, uint8_t *storage,
                        collector_t *collector)
{
    memset(collector, 0, sizeof(*collector));
    const gs232b_line_upload_t upload = {
        .ctx = collector, .begin = begin_upload, .abort = abort_upload
    };
    return gs232b_line_parser_init(parser, storage, GS232B_LINE_STORAGE_BYTES,
                                   upload) == GS232B_OK;
}

static bool events_equal(const collector_t *left, const collector_t *right)
{
    if (left->count != right->count) {
        return false;
    }
    for (size_t index = 0; index < left->count; ++index) {
        const collected_event_t *a = &left->events[index];
        const collected_event_t *b = &right->events[index];
        if (a->type != b->type || a->kind != b->kind || a->error != b->error ||
            a->size != b->size || a->is_position != b->is_position ||
            (a->type == GS232B_LINE_EVENT_COMMIT && a->size <= sizeof(a->bytes) &&
             memcmp(a->bytes, b->bytes, a->size) != 0)) {
            return false;
        }
        if (a->is_position &&
            (a->position.command != b->position.command ||
             a->position.format != b->position.format ||
             a->position.interval_s != b->position.interval_s ||
             a->position.point_count != b->position.point_count ||
             memcmp(a->angles, b->angles, sizeof(a->angles)) != 0)) {
            return false;
        }
    }
    return true;
}

static bool test_fragmentation_and_merged_lines(void)
{
    static const uint8_t input[] = {
        'r', '\r', '\n', ' ', 'c', '2', ' ', ' ', '\r',
        '!', 'N', 'E', 'T', ' ', 'S', 'E', 'T', ' ', '{', '"', 'n', 'a', 'm', 'e',
        '"', ':', '"', 'W', 'i', '-', 'F', 'i', ' ', '!', ' ',
        0xe4, 0xb8, 0xad, '"', '}', '\r', '\n',
        'x', '4', '\r', 'm', '1', '2', '\r'
    };
    uint8_t bytewise_storage[GS232B_LINE_STORAGE_BYTES];
    uint8_t chunked_storage[GS232B_LINE_STORAGE_BYTES];
    uint8_t merged_storage[GS232B_LINE_STORAGE_BYTES];
    gs232b_line_parser_t bytewise_parser;
    gs232b_line_parser_t chunked_parser;
    gs232b_line_parser_t merged_parser;
    collector_t bytewise;
    collector_t chunked;
    collector_t merged;
    CHECK(parser_init(&bytewise_parser, bytewise_storage, &bytewise));
    CHECK(parser_init(&chunked_parser, chunked_storage, &chunked));
    CHECK(parser_init(&merged_parser, merged_storage, &merged));

    for (size_t index = 0; index < sizeof(input); ++index) {
        CHECK(gs232b_line_parser_feed(&bytewise_parser, &input[index], 1, 100,
                                     collect_event, &bytewise) == GS232B_OK);
    }
    const size_t chunks[] = {2, 4, 1, 7, 3, 9, 2, 1, 11, 5};
    size_t offset = 0;
    size_t chunk = 0;
    while (offset < sizeof(input)) {
        size_t amount = chunks[chunk++ % (sizeof(chunks) / sizeof(chunks[0]))];
        if (amount > sizeof(input) - offset) {
            amount = sizeof(input) - offset;
        }
        CHECK(gs232b_line_parser_feed(&chunked_parser, input + offset, amount, 100,
                                     collect_event, &chunked) == GS232B_OK);
        offset += amount;
    }
    CHECK(gs232b_line_parser_feed(&merged_parser, input, sizeof(input), 100,
                                  collect_event, &merged) == GS232B_OK);

    CHECK(events_equal(&bytewise, &chunked));
    CHECK(events_equal(&bytewise, &merged));
    CHECK(merged.count == 5);
    CHECK(merged.events[0].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(merged.events[0].kind == GS232B_LINE_GS);
    CHECK(merged.events[0].size == 1 && merged.events[0].bytes[0] == 'R');
    CHECK(merged.events[1].kind == GS232B_LINE_GS);
    CHECK(merged.events[1].size == 2 && memcmp(merged.events[1].bytes, "C2", 2) == 0);
    CHECK(merged.events[2].kind == GS232B_LINE_EXTENSION);
    CHECK(merged.events[2].size == 31);
    CHECK(memcmp(merged.events[2].bytes, input + 9, merged.events[2].size) == 0);
    CHECK(merged.events[3].kind == GS232B_LINE_GS);
    CHECK(merged.events[3].size == 2 && memcmp(merged.events[3].bytes, "X4", 2) == 0);
    CHECK(merged.events[4].type == GS232B_LINE_EVENT_ERROR);
    CHECK(merged.events[4].error == GS232B_LINE_ERROR_INVALID_SYNTAX);
    return true;
}

static bool test_extension_header_after_spaces(void)
{
    uint8_t storage[GS232B_LINE_STORAGE_BYTES];
    gs232b_line_parser_t parser;
    collector_t events;
    CHECK(parser_init(&parser, storage, &events));
    static const uint8_t input[] = {' ', ' ', '!', 'N', 'e', 't', ' ', '{', '"', 'x',
                                    '"', ':', '"', 0xff, '"', '}', '\r'};
    CHECK(gs232b_line_parser_feed(&parser, input, sizeof(input), 100,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 1);
    CHECK(events.events[0].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[0].kind == GS232B_LINE_EXTENSION);
    CHECK(events.events[0].size == sizeof(input) - 3);
    CHECK(events.events[0].bytes[0] == '!');
    CHECK(events.events[0].bytes[3] == 't');
    CHECK(events.events[0].bytes[11] == 0xff);
    return true;
}

static bool test_commit_and_crlf(void)
{
    uint8_t storage[GS232B_LINE_STORAGE_BYTES];
    gs232b_line_parser_t parser;
    collector_t events;
    CHECK(parser_init(&parser, storage, &events));
    static const uint8_t partial[] = {'M', '1', '8', '0'};
    CHECK(gs232b_line_parser_feed(&parser, partial, sizeof(partial), 500,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 0);
    static const uint8_t completed[] = {'\r', '\n', 'C', '\r', '\n'};
    CHECK(gs232b_line_parser_feed(&parser, completed, sizeof(completed), 501,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 2);
    CHECK(events.events[0].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[0].is_position);
    CHECK(events.events[0].position.command == 'M');
    CHECK(events.events[0].position.format == GS232B_POSITION_SINGLE);
    CHECK(events.events[0].angles[0] == 180);
    CHECK(events.events[1].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[1].size == 1 && events.events[1].bytes[0] == 'C');
    return true;
}

static bool test_gs_syntax_before_commit(void)
{
    uint8_t storage[GS232B_LINE_STORAGE_BYTES];
    gs232b_line_parser_t parser;
    collector_t events;
    CHECK(parser_init(&parser, storage, &events));
    static const uint8_t input[] = {
        'm', '1', '8', '0', '\r',
        'm', '1', '2', '\r',
        'm', ' ', '1', '8', '0', '\r',
        'm', '0', '1', '0', ' ', '1', '5', '0', '\r',
        'm', '0', '1', '0', ' ', ' ', '1', '5', '0', ' ', '1', '4', '0', '\r',
        'w', '1', '8', '0', ' ', '0', '9', '0', '\r',
        'w', '0', '1', '0', ' ', '1', '8', '0', ' ', '0', '9', '0', ' ',
        '1', '2', '0', ' ', '0', '3', '0', '\r',
        'w', '0', '1', '0', ' ', '1', '8', '0', ' ', '0', '9', '0', ' ',
        '1', '2', '0', '\r'
    };
    CHECK(gs232b_line_parser_feed(&parser, input, sizeof(input), 100,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 8);
    CHECK(events.events[0].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[0].is_position);
    CHECK(events.events[0].position.format == GS232B_POSITION_SINGLE);
    CHECK(events.events[0].angles[0] == 180);
    CHECK(events.events[1].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[1].error == GS232B_LINE_ERROR_INVALID_SYNTAX);
    CHECK(events.events[2].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[2].error == GS232B_LINE_ERROR_INVALID_SYNTAX);
    CHECK(events.events[3].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[3].error == GS232B_LINE_ERROR_INVALID_SYNTAX);
    CHECK(events.events[4].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[4].is_position);
    CHECK(events.events[4].position.format == GS232B_POSITION_SEQUENCE);
    CHECK(events.events[4].position.interval_s == 10);
    CHECK(events.events[4].position.point_count == 2);
    CHECK(events.events[4].angles[0] == 150 && events.events[4].angles[1] == 140);
    CHECK(events.events[5].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[5].is_position);
    CHECK(events.events[5].position.command == 'W');
    CHECK(events.events[5].position.format == GS232B_POSITION_SINGLE);
    CHECK(events.events[5].angles[0] == 180 && events.events[5].angles[1] == 90);
    CHECK(events.events[6].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[7].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[7].error == GS232B_LINE_ERROR_INVALID_SYNTAX);
    return true;
}

static bool test_strict_ascii_spacing_and_numbers(void)
{
    static const uint8_t text[] = "  m180   090  ";
    gs232b_byte_span_t trimmed = gs232b_byte_span_trim_spaces(
        (gs232b_byte_span_t){.bytes = text, .size = sizeof(text) - 1});
    CHECK(trimmed.size == 10);
    gs232b_byte_span_t remaining = trimmed;
    gs232b_byte_span_t token;
    CHECK(gs232b_byte_span_next_space_token(&remaining, &token));
    CHECK(gs232b_byte_span_equal_ascii_ci(token, "M180"));
    CHECK(gs232b_byte_span_next_space_token(&remaining, &token));
    CHECK(gs232b_byte_span_equal_ascii_ci(token, "090"));
    CHECK(!gs232b_byte_span_next_space_token(&remaining, &token));

    uint16_t value = 99;
    CHECK(gs232b_byte_span_parse_decimal3(
        (gs232b_byte_span_t){.bytes = (const uint8_t *)"007", .size = 3}, &value));
    CHECK(value == 7);
    CHECK(!gs232b_byte_span_parse_decimal3(
        (gs232b_byte_span_t){.bytes = (const uint8_t *)"07", .size = 2}, &value));
    CHECK(!gs232b_byte_span_parse_decimal3(
        (gs232b_byte_span_t){.bytes = (const uint8_t *)"+07", .size = 3}, &value));
    CHECK(!gs232b_byte_span_parse_decimal3(
        (gs232b_byte_span_t){.bytes = (const uint8_t *)"0 7", .size = 3}, &value));
    CHECK(gs232b_byte_span_trim_spaces(
        (gs232b_byte_span_t){.bytes = (const uint8_t *)"\tR", .size = 2}).bytes[0] == '\t');
    return true;
}

static bool test_invalid_line_discards_prefix_and_suffix_once(void)
{
    uint8_t storage[GS232B_LINE_STORAGE_BYTES];
    gs232b_line_parser_t parser;
    collector_t events;
    CHECK(parser_init(&parser, storage, &events));
    static const uint8_t input[] = {'R', 0, 'S'};
    CHECK(gs232b_line_parser_feed(&parser, input, sizeof(input), 100,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 0);
    static const uint8_t finish_input[] = {'\r', 'L', '\r', 'R', '\n', 'S', '\r'};
    CHECK(gs232b_line_parser_feed(&parser, finish_input, sizeof(finish_input), 100,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 3);
    CHECK(events.events[0].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[0].error == GS232B_LINE_ERROR_INVALID_BYTE);
    CHECK(events.events[1].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[1].size == 1 && events.events[1].bytes[0] == 'L');
    CHECK(events.events[2].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[2].error == GS232B_LINE_ERROR_INVALID_BYTE);

    static const uint8_t extension_control[] = {'!', 'a', 0, 'b', '\r', 'R', '\r'};
    CHECK(gs232b_line_parser_feed(&parser, extension_control, sizeof(extension_control), 101,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 5);
    CHECK(events.events[3].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[3].kind == GS232B_LINE_EXTENSION);
    CHECK(events.events[4].type == GS232B_LINE_EVENT_COMMIT);

    static const uint8_t extended_byte[] = {'!', 'x', ' ', 0xff, '\r', 'R', '\r'};
    CHECK(gs232b_line_parser_feed(&parser, extended_byte, sizeof(extended_byte), 102,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 7);
    CHECK(events.events[5].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[5].kind == GS232B_LINE_EXTENSION);
    CHECK(events.events[5].size == 4 && events.events[5].bytes[3] == 0xff);
    CHECK(events.events[6].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[6].size == 1 && events.events[6].bytes[0] == 'R');

    static const uint8_t tab_line[] = {'R', '\t', 'S', '\r', 'L', '\r'};
    CHECK(gs232b_line_parser_feed(&parser, tab_line, sizeof(tab_line), 103,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 9);
    CHECK(events.events[7].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[8].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[8].size == 1 && events.events[8].bytes[0] == 'L');

    static const uint8_t high_gs[] = {'R', 0xff, 'S', '\r', 'L', '\r'};
    CHECK(gs232b_line_parser_feed(&parser, high_gs, sizeof(high_gs), 104,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 11);
    CHECK(events.events[9].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[10].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[10].bytes[0] == 'L');
    return true;
}

static bool test_length_limits(void)
{
    static uint8_t storage[GS232B_LINE_STORAGE_BYTES];
    static uint8_t oversized[GS232B_GS_LINE_MAX_BYTES + 2];
    gs232b_line_parser_t parser;
    collector_t events;
    CHECK(parser_init(&parser, storage, &events));

    static uint8_t max_gs[GS232B_GS_LINE_MAX_BYTES + 1];
    memset(max_gs, ' ', GS232B_GS_LINE_MAX_BYTES);
    max_gs[GS232B_GS_LINE_MAX_BYTES] = '\r';
    CHECK(gs232b_line_parser_feed(&parser, max_gs, sizeof(max_gs), 199,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 1);
    CHECK(events.events[0].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[0].size == 0);

    memset(oversized, ' ', sizeof(oversized));
    oversized[GS232B_GS_LINE_MAX_BYTES + 1] = '\r';
    CHECK(gs232b_line_parser_feed(&parser, oversized, sizeof(oversized), 200,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 2);
    CHECK(events.events[1].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[1].error == GS232B_LINE_ERROR_TOO_LONG);

    static uint8_t extension[GS232B_EXTENSION_LINE_MAX_BYTES + 2];
    extension[0] = '!';
    memset(extension + 1, 'a', GS232B_EXTENSION_LINE_MAX_BYTES - 1);
    extension[GS232B_EXTENSION_LINE_MAX_BYTES] = '\r';
    CHECK(gs232b_line_parser_feed(&parser, extension,
                                 GS232B_EXTENSION_LINE_MAX_BYTES + 1, 201,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 3);
    CHECK(events.events[2].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[2].kind == GS232B_LINE_EXTENSION);
    CHECK(events.events[2].size == GS232B_EXTENSION_LINE_MAX_BYTES);

    memset(extension + 1, 'a', GS232B_EXTENSION_LINE_MAX_BYTES);
    extension[sizeof(extension) - 1] = '\r';
    CHECK(gs232b_line_parser_feed(&parser, extension, sizeof(extension), 202,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 4);
    CHECK(events.events[3].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[3].error == GS232B_LINE_ERROR_TOO_LONG);

    static const uint8_t valid[] = {'!', 'x', '\r'};
    CHECK(gs232b_line_parser_feed(&parser, valid, sizeof(valid), 203,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 5);
    CHECK(events.events[4].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[4].kind == GS232B_LINE_EXTENSION);
    return true;
}

static bool test_half_line_timeout_and_recovery(void)
{
    uint8_t storage[GS232B_LINE_STORAGE_BYTES];
    gs232b_line_parser_t parser;
    collector_t events;
    CHECK(parser_init(&parser, storage, &events));
    static const uint8_t partial[] = {'R'};
    CHECK(gs232b_line_parser_feed(&parser, partial, sizeof(partial), 1000,
                                 collect_event, &events) == GS232B_OK);
    CHECK(gs232b_line_parser_tick(&parser, 1000 + GS232B_LINE_PARTIAL_TIMEOUT_US - 1,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 0);
    CHECK(gs232b_line_parser_tick(&parser, 1000 + GS232B_LINE_PARTIAL_TIMEOUT_US,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 1);
    CHECK(events.events[0].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[0].error == GS232B_LINE_ERROR_TIMEOUT);
    CHECK(gs232b_line_parser_tick(&parser, 1000 + 3 * GS232B_LINE_PARTIAL_TIMEOUT_US,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 1);

    static const uint8_t stale_remainder[] = {'C', '2', '\r', '\n', 'R', '\r'};
    CHECK(gs232b_line_parser_feed(&parser, stale_remainder, sizeof(stale_remainder),
                                 1000 + 3 * GS232B_LINE_PARTIAL_TIMEOUT_US,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 2);
    CHECK(events.events[1].type == GS232B_LINE_EVENT_COMMIT);
    CHECK(events.events[1].size == 1 && events.events[1].bytes[0] == 'R');

    static const uint8_t arrival_after_timeout[] = {'C', '\r'};
    CHECK(gs232b_line_parser_feed(&parser, partial, sizeof(partial), 9000000,
                                 collect_event, &events) == GS232B_OK);
    CHECK(gs232b_line_parser_feed(&parser, arrival_after_timeout,
                                 sizeof(arrival_after_timeout),
                                 9000000 + GS232B_LINE_PARTIAL_TIMEOUT_US,
                                 collect_event, &events) == GS232B_OK);
    CHECK(events.count == 3);
    CHECK(events.events[2].type == GS232B_LINE_EVENT_ERROR);
    CHECK(events.events[2].error == GS232B_LINE_ERROR_TIMEOUT);
    return true;
}

int main(void)
{
    if (!test_fragmentation_and_merged_lines() || !test_extension_header_after_spaces() ||
        !test_commit_and_crlf() ||
        !test_gs_syntax_before_commit() ||
        !test_strict_ascii_spacing_and_numbers() ||
        !test_invalid_line_discards_prefix_and_suffix_once() ||
        !test_length_limits() || !test_half_line_timeout_and_recovery()) {
        return 1;
    }
    puts("line parser tests passed");
    return 0;
}
