#include "gs232b/response_encoder.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

#define ENCODED_CAPACITY 512U

static bool assert_golden(gs232b_response_t response,
                          const uint8_t *expected, size_t expected_size)
{
    uint8_t actual[ENCODED_CAPACITY];
    memset(actual, 0xa5, sizeof(actual));
    size_t actual_size = 0;
    CHECK(gs232b_response_encode(&response, actual, sizeof(actual), &actual_size) == GS232B_OK);
    CHECK(actual_size == expected_size);
    CHECK(memcmp(actual, expected, expected_size) == 0);
    CHECK(actual[expected_size] == 0xa5);
    return true;
}

static bool test_status_and_queries(void)
{
    static const uint8_t success[] = "\r";
    static const uint8_t error[] = "?>\r\n";
    static const uint8_t azimuth[] = "AZ=007\r\n";
    static const uint8_t elevation[] = "EL=180\r\n";
    static const uint8_t both[] = "AZ=450  EL=003\r\n";
    static const uint8_t sequence[] = "=0001=3800\r\n";
    static const uint8_t empty_sequence[] = "=0000=0000\r\n";

    CHECK(assert_golden((gs232b_response_t){.kind = GS232B_RESPONSE_SUCCESS},
                        success, sizeof(success) - 1));
    CHECK(assert_golden((gs232b_response_t){.kind = GS232B_RESPONSE_ERROR},
                        error, sizeof(error) - 1));
    CHECK(assert_golden((gs232b_response_t){
                            .kind = GS232B_RESPONSE_AZIMUTH,
                            .data.angles.azimuth_degrees = 7
                        }, azimuth, sizeof(azimuth) - 1));
    CHECK(assert_golden((gs232b_response_t){
                            .kind = GS232B_RESPONSE_ELEVATION,
                            .data.angles.elevation_degrees = 180
                        }, elevation, sizeof(elevation) - 1));
    CHECK(assert_golden((gs232b_response_t){
                            .kind = GS232B_RESPONSE_BOTH_ANGLES,
                            .data.angles.azimuth_degrees = 450,
                            .data.angles.elevation_degrees = 3
                        }, both, sizeof(both) - 1));
    CHECK(assert_golden((gs232b_response_t){
                            .kind = GS232B_RESPONSE_SEQUENCE,
                            .data.sequence.current_point = 1,
                            .data.sequence.point_count = 3800
                        }, sequence, sizeof(sequence) - 1));
    CHECK(assert_golden((gs232b_response_t){
                            .kind = GS232B_RESPONSE_SEQUENCE
                        }, empty_sequence, sizeof(empty_sequence) - 1));
    return true;
}

static bool test_help_fixtures(void)
{
    static const uint8_t help_azimuth[] =
        "R / L : Azimuth clockwise / counterclockwise\r\n"
        "A : Stop azimuth\r\n"
        "S : Stop all and clear sequence\r\n"
        "Maaa : Set azimuth\r\n"
        "Msss aaa bbb ... : Load azimuth sequence\r\n"
        "C / C2 : Read azimuth / azimuth and elevation\r\n"
        "X1 / X2 / X3 / X4 : Azimuth speed\r\n"
        "T / N : Start sequence / read sequence index and count\r\n"
        "O / F : Azimuth offset / calibration display\r\n"
        "H / H2 / H3 : Azimuth / elevation / display help\r\n";
    static const uint8_t help_elevation[] =
        "U / D : Elevation up / down\r\n"
        "E : Stop elevation\r\n"
        "S : Stop all and clear sequence\r\n"
        "Waaa eee : Set azimuth and elevation\r\n"
        "Wsss aaa eee bbb fff ... : Load azimuth/elevation sequence\r\n"
        "B / C2 : Read elevation / azimuth and elevation\r\n"
        "T / N : Start sequence / read sequence index and count\r\n"
        "O2 / F2 : Elevation offset / calibration display\r\n";
    static const uint8_t help_display_360_south[] =
        "P36 : 360 Degree mode\r\n"
        "P45 : 450 Degree mode\r\n"
        "Z : Toggle N/S center in 360 Degree mode\r\n"
        "mode 360 Degree\r\n"
        "S Center\r\n";
    static const uint8_t help_display_450_north[] =
        "P36 : 360 Degree mode\r\n"
        "P45 : 450 Degree mode\r\n"
        "Z : Toggle N/S center in 360 Degree mode\r\n"
        "mode 450 Degree\r\n"
        "N Center\r\n";

    CHECK(assert_golden((gs232b_response_t){.kind = GS232B_RESPONSE_HELP_AZIMUTH},
                        help_azimuth, sizeof(help_azimuth) - 1));
    CHECK(assert_golden((gs232b_response_t){.kind = GS232B_RESPONSE_HELP_ELEVATION},
                        help_elevation, sizeof(help_elevation) - 1));
    CHECK(assert_golden((gs232b_response_t){
                            .kind = GS232B_RESPONSE_HELP_DISPLAY,
                            .data.display.mode_450 = false,
                            .data.display.south_center = true
                        }, help_display_360_south, sizeof(help_display_360_south) - 1));
    CHECK(assert_golden((gs232b_response_t){
                            .kind = GS232B_RESPONSE_HELP_DISPLAY,
                            .data.display.mode_450 = true,
                            .data.display.south_center = false
                        }, help_display_450_north, sizeof(help_display_450_north) - 1));
    return true;
}

static bool test_calibration_fixtures(void)
{
    static const uint8_t zero_azimuth[] = "AZoffset\r\nare you sure? yes or no\r\n";
    static const uint8_t zero_elevation[] = "ELoffset\r\nare you sure? yes or no\r\n";
    static const uint8_t completed[] = "Completed\r\n";
    static const uint8_t calibration_azimuth[] = "AZ=090\r\n";
    static const uint8_t calibration_both[] = "AZ=450  EL=180\r\n";

    CHECK(assert_golden((gs232b_response_t){
                            .kind = GS232B_RESPONSE_ZERO_AZIMUTH_PROMPT
                        }, zero_azimuth, sizeof(zero_azimuth) - 1));
    CHECK(assert_golden((gs232b_response_t){
                            .kind = GS232B_RESPONSE_ZERO_ELEVATION_PROMPT
                        }, zero_elevation, sizeof(zero_elevation) - 1));
    CHECK(assert_golden((gs232b_response_t){
                            .kind = GS232B_RESPONSE_CALIBRATION_COMPLETE
                        }, completed, sizeof(completed) - 1));
    CHECK(assert_golden((gs232b_response_t){
                            .kind = GS232B_RESPONSE_CALIBRATION_AZIMUTH,
                            .data.angles.azimuth_degrees = 90
                        }, calibration_azimuth, sizeof(calibration_azimuth) - 1));
    CHECK(assert_golden((gs232b_response_t){
                            .kind = GS232B_RESPONSE_CALIBRATION_BOTH,
                            .data.angles.azimuth_degrees = 450,
                            .data.angles.elevation_degrees = 180
                        }, calibration_both, sizeof(calibration_both) - 1));
    return true;
}

static bool test_invalid_and_small_buffers(void)
{
    gs232b_response_t success = {.kind = GS232B_RESPONSE_SUCCESS};
    uint8_t output[8];
    memset(output, 0xa5, sizeof(output));
    size_t size = 0;

    CHECK(gs232b_response_encode(&success, NULL, 0, &size) ==
          GS232B_ERROR_BUFFER_TOO_SMALL);
    CHECK(size == 1);
    CHECK(gs232b_response_encode(&success, output, 0, &size) ==
          GS232B_ERROR_BUFFER_TOO_SMALL);
    CHECK(size == 1);
    for (size_t index = 0; index < sizeof(output); ++index) {
        CHECK(output[index] == 0xa5);
    }

    gs232b_response_t invalid_angle = {
        .kind = GS232B_RESPONSE_AZIMUTH,
        .data.angles.azimuth_degrees = 1000
    };
    CHECK(gs232b_response_encode(&invalid_angle, output, sizeof(output), &size) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(size == 0);
    CHECK(gs232b_response_encode(NULL, output, sizeof(output), &size) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_response_encode(&success, NULL, sizeof(output), &size) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_response_encode(&success, output, sizeof(output), NULL) ==
          GS232B_ERROR_INVALID_ARGUMENT);
    return true;
}

int main(void)
{
    if (!test_status_and_queries() || !test_help_fixtures() ||
        !test_calibration_fixtures() || !test_invalid_and_small_buffers()) {
        return 1;
    }
    return 0;
}
