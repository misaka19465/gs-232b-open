#include "gs232b/position_parser.h"

static gs232b_error_t finish_field(gs232b_position_parser_t *parser)
{
    if (parser->digits != 3) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (parser->fields == 0) {
        parser->header = parser->value;
    } else {
        if (parser->fields > GS232B_POSITION_MAX_ANGLES) {
            return GS232B_ERROR_RESOURCE_EXHAUSTED;
        }
        if (parser->angles != NULL) {
            parser->angles[parser->fields - 1] = parser->value;
        }
    }
    ++parser->fields;
    parser->digits = 0;
    parser->value = 0;
    return GS232B_OK;
}

void gs232b_position_parser_init(gs232b_position_parser_t *parser, char command,
                                 uint16_t *angles)
{
    *parser = (gs232b_position_parser_t){.angles = angles, .command = command};
}

gs232b_error_t gs232b_position_parser_push(gs232b_position_parser_t *parser,
                                           uint8_t byte)
{
    if (byte == ' ') {
        if (parser->fields == 0 && parser->digits == 0) {
            parser->leading_space = true;
        }
        return parser->digits == 0 ? GS232B_OK : finish_field(parser);
    }
    if (byte < '0' || byte > '9' || parser->leading_space || parser->digits == 3) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    parser->value = (uint16_t)(parser->value * 10U + byte - '0');
    ++parser->digits;
    return GS232B_OK;
}

gs232b_error_t gs232b_position_parser_finish(gs232b_position_parser_t *parser,
                                             gs232b_position_command_t *command)
{
    if (parser->digits != 0) {
        const gs232b_error_t error = finish_field(parser);
        if (error != GS232B_OK) {
            return error;
        }
    }
    *command = (gs232b_position_command_t){.command = parser->command};
    if (parser->fields == 0) {
        return GS232B_OK;
    }
    const bool paired = parser->command == 'W';
    if (parser->fields == (paired ? 2U : 1U)) {
        command->format = GS232B_POSITION_SINGLE;
        command->point_count = 1;
        if (parser->angles != NULL) {
            if (paired) {
                parser->angles[1] = parser->angles[0];
            }
            parser->angles[0] = parser->header;
        }
    } else {
        const size_t angles = parser->fields - 1;
        if (angles < (paired ? 4U : 2U) || (paired && angles % 2 != 0)) {
            return GS232B_ERROR_INVALID_ARGUMENT;
        }
        command->format = GS232B_POSITION_SEQUENCE;
        command->interval_s = parser->header;
        command->point_count = (uint16_t)(angles / (paired ? 2U : 1U));
    }
    command->angles = parser->angles;
    return GS232B_OK;
}
