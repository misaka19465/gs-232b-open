#include "gs232b/response_encoder.h"

#include <string.h>

#define GS232B_RESPONSE_MAX_BYTES 512U

typedef struct {
    uint8_t bytes[GS232B_RESPONSE_MAX_BYTES];
    size_t size;
} response_builder_t;

static bool append_bytes(response_builder_t *builder, const char *bytes, size_t size)
{
    if (size > sizeof(builder->bytes) - builder->size) {
        return false;
    }
    memcpy(builder->bytes + builder->size, bytes, size);
    builder->size += size;
    return true;
}

static bool append_text(response_builder_t *builder, const char *text)
{
    return append_bytes(builder, text, strlen(text));
}

static bool append_decimal3(response_builder_t *builder, uint16_t value)
{
    if (value > 999) {
        return false;
    }
    char digits[3] = {
        (char)('0' + value / 100),
        (char)('0' + (value / 10) % 10),
        (char)('0' + value % 10)
    };
    return append_bytes(builder, digits, sizeof(digits));
}

static bool append_decimal4(response_builder_t *builder, uint16_t value)
{
    if (value > 9999) {
        return false;
    }
    char digits[4] = {
        (char)('0' + value / 1000),
        (char)('0' + (value / 100) % 10),
        (char)('0' + (value / 10) % 10),
        (char)('0' + value % 10)
    };
    return append_bytes(builder, digits, sizeof(digits));
}

static bool append_angle(response_builder_t *builder, const char *prefix, uint16_t value)
{
    return append_text(builder, prefix) && append_decimal3(builder, value) &&
           append_text(builder, "\r\n");
}

static bool append_help_display(response_builder_t *builder,
                                const gs232b_response_t *response)
{
    return append_text(builder,
                       "P36 : 360 Degree mode\r\n"
                       "P45 : 450 Degree mode\r\n"
                       "Z : Toggle N/S center in 360 Degree mode\r\n") &&
           append_text(builder, response->data.display.mode_450
                                    ? "mode 450 Degree\r\n"
                                    : "mode 360 Degree\r\n") &&
           append_text(builder, response->data.display.south_center
                                    ? "S Center\r\n"
                                    : "N Center\r\n");
}

gs232b_error_t gs232b_response_encode(const gs232b_response_t *response,
                                      uint8_t *buffer, size_t capacity,
                                      size_t *size)
{
    if (size == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *size = 0;
    if (response == NULL || (capacity != 0 && buffer == NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }

    response_builder_t builder = {0};
    bool valid = false;
    switch (response->kind) {
    case GS232B_RESPONSE_SUCCESS:
        valid = append_text(&builder, "\r");
        break;
    case GS232B_RESPONSE_ERROR:
        valid = append_text(&builder, "?>\r\n");
        break;
    case GS232B_RESPONSE_AZIMUTH:
        valid = append_angle(&builder, "AZ=", response->data.angles.azimuth_degrees);
        break;
    case GS232B_RESPONSE_ELEVATION:
        valid = append_angle(&builder, "EL=", response->data.angles.elevation_degrees);
        break;
    case GS232B_RESPONSE_BOTH_ANGLES:
    case GS232B_RESPONSE_CALIBRATION_BOTH:
        valid = append_text(&builder, "AZ=") &&
                append_decimal3(&builder, response->data.angles.azimuth_degrees) &&
                append_text(&builder, "  EL=") &&
                append_decimal3(&builder, response->data.angles.elevation_degrees) &&
                append_text(&builder, "\r\n");
        break;
    case GS232B_RESPONSE_SEQUENCE:
        valid = append_text(&builder, "=") &&
                append_decimal4(&builder, response->data.sequence.current_point) &&
                append_text(&builder, "=") &&
                append_decimal4(&builder, response->data.sequence.point_count) &&
                append_text(&builder, "\r\n");
        break;
    case GS232B_RESPONSE_HELP_AZIMUTH:
        valid = append_text(&builder,
                            "R / L : Azimuth clockwise / counterclockwise\r\n"
                            "A : Stop azimuth\r\n"
                            "S : Stop all and clear sequence\r\n"
                            "Maaa : Set azimuth\r\n"
                            "Msss aaa bbb ... : Load azimuth sequence\r\n"
                            "C / C2 : Read azimuth / azimuth and elevation\r\n"
                            "X1 / X2 / X3 / X4 : Azimuth speed\r\n"
                            "T / N : Start sequence / read sequence index and count\r\n"
                            "O / F : Azimuth offset / calibration display\r\n"
                            "H / H2 / H3 : Azimuth / elevation / display help\r\n");
        break;
    case GS232B_RESPONSE_HELP_ELEVATION:
        valid = append_text(&builder,
                            "U / D : Elevation up / down\r\n"
                            "E : Stop elevation\r\n"
                            "S : Stop all and clear sequence\r\n"
                            "Waaa eee : Set azimuth and elevation\r\n"
                            "Wsss aaa eee bbb fff ... : Load azimuth/elevation sequence\r\n"
                            "B / C2 : Read elevation / azimuth and elevation\r\n"
                            "T / N : Start sequence / read sequence index and count\r\n"
                            "O2 / F2 : Elevation offset / calibration display\r\n");
        break;
    case GS232B_RESPONSE_HELP_DISPLAY:
        valid = append_help_display(&builder, response);
        break;
    case GS232B_RESPONSE_ZERO_AZIMUTH_PROMPT:
        valid = append_text(&builder, "AZoffset\r\nare you sure? yes or no\r\n");
        break;
    case GS232B_RESPONSE_ZERO_ELEVATION_PROMPT:
        valid = append_text(&builder, "ELoffset\r\nare you sure? yes or no\r\n");
        break;
    case GS232B_RESPONSE_CALIBRATION_COMPLETE:
        valid = append_text(&builder, "Completed\r\n");
        break;
    case GS232B_RESPONSE_CALIBRATION_AZIMUTH:
        valid = append_angle(&builder, "AZ=", response->data.angles.azimuth_degrees);
        break;
    default:
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (!valid) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }

    *size = builder.size;
    if (buffer == NULL || capacity < builder.size) {
        return GS232B_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(buffer, builder.bytes, builder.size);
    return GS232B_OK;
}
