#include "gs232b/provisioning.h"
#include "gs232b/extension.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

typedef struct {
    gs232b_provisioning_ap_t aps[80];
    gs232b_time_us_t now;
    size_t count, reported_count;
    gs232b_error_t error;
    unsigned calls;
    bool force_truncated;
} fixture_t;

static gs232b_time_us_t clock_now(void *ctx)
{
    return ((fixture_t *)ctx)->now;
}

static gs232b_error_t scan(void *ctx, gs232b_provisioning_ap_t *aps,
    size_t capacity, size_t *count, bool *truncated)
{
    fixture_t *fixture = ctx;
    ++fixture->calls;
    if (fixture->error != GS232B_OK) return fixture->error;
    const size_t actual = fixture->count < capacity ? fixture->count : capacity;
    memcpy(aps, fixture->aps, actual * sizeof(*aps));
    *count = fixture->reported_count == 0 ? actual : fixture->reported_count;
    *truncated = fixture->force_truncated || fixture->count > capacity;
    return GS232B_OK;
}

static void set_ap(fixture_t *fixture, size_t index, const char *ssid, int rssi)
{
    snprintf(fixture->aps[index].ssid, sizeof(fixture->aps[index].ssid), "%s", ssid);
    fixture->aps[index].rssi = (int8_t)rssi;
    fixture->aps[index].auth = (uint8_t)(index % 4U);
}

static bool has_field(gs232b_json_value_t object, const char *name, uint32_t expected)
{
    gs232b_json_value_t value;
    uint32_t actual = 0;
    return gs232b_json_get(object, name, &value) == GS232B_OK &&
        gs232b_json_uint32(value, &actual) == GS232B_OK && actual == expected;
}

static bool test_snapshot_pages_and_expiration(void)
{
    fixture_t fixture = {0};
    gs232b_provisioning_t provisioning = {0};
    provisioning.clock = (gs232b_clock_t){.ctx = &fixture, .now_us = clock_now};
    provisioning.ap_port = (gs232b_provisioning_ap_port_t){.ctx = &fixture, .scan = scan};
    provisioning.status.state = GS232B_PROVISIONING_ACTIVE;
    provisioning.initialized = true;
    for (size_t i = 0; i < 64; ++i) {
        char name[24];
        snprintf(name, sizeof(name), "network-%02u", (unsigned)i);
        set_ap(&fixture, i, name, -80 - (int)(i % 20U));
    }
    fixture.count = 64;
    memset(fixture.aps[0].ssid, 'x', GS232B_WIFI_STA_SSID_MAX + 1U);
    fixture.aps[0].rssi = -10;
    memcpy(fixture.aps[1].ssid, "\xc0\xaf", 3);
    fixture.aps[1].rssi = -9;
    set_ap(&fixture, 2, "radio\"\\\n", -30);
    set_ap(&fixture, 3, "weaker", -75);
    set_ap(&fixture, 4, "stronger", -25);

    uint8_t first[GS232B_EXTENSION_DATA_MAX_BYTES], replay[GS232B_EXTENSION_DATA_MAX_BYTES];
    size_t first_size = 0, replay_size = 0;
    CHECK(gs232b_provisioning_scan_page(&provisioning, 0, 0,
        first, sizeof(first), &first_size) == GS232B_OK);
    CHECK(fixture.calls == 1 && provisioning.scan.id == 1);
    CHECK(provisioning.scan.count == 62 && !provisioning.scan.truncated);
    CHECK(strcmp(provisioning.scan.aps[0].ssid, "stronger") == 0);
    CHECK(strcmp(provisioning.scan.aps[1].ssid, "radio\"\\\n") == 0);
    CHECK(provisioning.scan.aps[0].rssi == -25);
    first[first_size] = 0;
    CHECK(strstr((char *)first, "radio\\\"\\\\\\u000a") != NULL);
    CHECK(strstr((char *)first, "\"offset\":0,\"total\":62,\"next\":8") != NULL);
    CHECK(gs232b_json_parse((gs232b_byte_span_t){first, first_size},
        &(gs232b_json_value_t){0}) == GS232B_OK);
    CHECK(gs232b_provisioning_scan_page(&provisioning, 1, 0,
        replay, sizeof(replay), &replay_size) == GS232B_OK);
    CHECK(first_size == replay_size && memcmp(first, replay, first_size) == 0);
    CHECK(fixture.calls == 1);
    uint8_t second[GS232B_EXTENSION_DATA_MAX_BYTES];
    size_t second_size = 0;
    CHECK(gs232b_provisioning_scan_page(&provisioning, 1, 8,
        second, sizeof(second), &second_size) == GS232B_OK);
    gs232b_json_value_t page;
    CHECK(gs232b_json_parse((gs232b_byte_span_t){second, second_size}, &page) == GS232B_OK);
    CHECK(has_field(page, "offset", 8) && has_field(page, "total", 62) &&
        has_field(page, "next", 16));
    CHECK(gs232b_provisioning_scan_page(&provisioning, 1, 0,
        replay, 1, &replay_size) == GS232B_ERROR_BUFFER_TOO_SMALL);
    CHECK(gs232b_provisioning_scan_page(&provisioning, 1, 63,
        replay, sizeof(replay), &replay_size) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_provisioning_scan_page(&provisioning, 1, 0,
        replay, sizeof(replay), &replay_size) == GS232B_OK);

    fixture.count = 70;
    fixture.force_truncated = true;
    CHECK(gs232b_provisioning_scan_page(&provisioning, 0, 0,
        replay, sizeof(replay), &replay_size) == GS232B_OK);
    CHECK(provisioning.scan.id == 2 && fixture.calls == 2 && provisioning.scan.truncated);
    CHECK(gs232b_provisioning_scan_page(&provisioning, 1, 8,
        replay, sizeof(replay), &replay_size) == GS232B_ERROR_CONFLICT);
    fixture.now += GS232B_PROVISIONING_SCAN_TTL_US;
    CHECK(gs232b_provisioning_scan_page(&provisioning, 2, 8,
        replay, sizeof(replay), &replay_size) == GS232B_ERROR_CONFLICT);
    provisioning.status.state = GS232B_PROVISIONING_OFF;
    CHECK(gs232b_provisioning_scan_page(&provisioning, 0, 0,
        replay, sizeof(replay), &replay_size) == GS232B_ERROR_NOT_READY);
    return true;
}

static bool test_scan_failure_and_callback_contract(void)
{
    fixture_t fixture = {0};
    gs232b_provisioning_t provisioning = {0};
    provisioning.clock = (gs232b_clock_t){.ctx = &fixture, .now_us = clock_now};
    provisioning.ap_port = (gs232b_provisioning_ap_port_t){.ctx = &fixture, .scan = scan};
    provisioning.status.state = GS232B_PROVISIONING_ACTIVE;
    provisioning.initialized = true;
    set_ap(&fixture, 0, "single", -42);
    fixture.count = 1;
    fixture.error = GS232B_ERROR_IO;
    uint8_t page[GS232B_EXTENSION_DATA_MAX_BYTES];
    size_t size = 0;
    CHECK(gs232b_provisioning_scan_page(&provisioning, 0, 0,
        page, sizeof(page), &size) == GS232B_ERROR_IO);
    CHECK(!provisioning.scan.valid);
    fixture.error = GS232B_OK;
    fixture.reported_count = GS232B_PROVISIONING_SCAN_MAX_APS + 1U;
    CHECK(gs232b_provisioning_scan_page(&provisioning, 0, 0,
        page, sizeof(page), &size) == GS232B_ERROR_CORRUPT);
    CHECK(!provisioning.scan.valid);
    fixture.reported_count = 0;
    provisioning.ap_port.scan = NULL;
    CHECK(gs232b_provisioning_scan_page(&provisioning, 0, 0,
        page, sizeof(page), &size) == GS232B_ERROR_NOT_READY);
    provisioning.status.state = GS232B_PROVISIONING_EXPIRED;
    CHECK(gs232b_provisioning_scan_page(&provisioning, 1, 0,
        page, sizeof(page), &size) == GS232B_ERROR_NOT_READY);
    return true;
}

static bool test_worst_case_escaped_pages(void)
{
    fixture_t fixture = {.count = GS232B_PROVISIONING_SCAN_MAX_APS};
    gs232b_provisioning_t provisioning = {
        .clock = {.ctx = &fixture, .now_us = clock_now},
        .ap_port = {.ctx = &fixture, .scan = scan},
        .status = {.state = GS232B_PROVISIONING_ACTIVE},
        .scan = {.id = UINT32_MAX - 1U},
        .initialized = true
    };
    for (size_t i = 0; i < fixture.count; ++i) {
        memset(fixture.aps[i].ssid, '\x1f', 31);
        fixture.aps[i].ssid[31] = 'A' + (int)(i % 26U);
        fixture.aps[i].rssi = INT8_MIN;
        fixture.aps[i].auth = UINT8_MAX;
    }
    uint8_t bytes[GS232B_EXTENSION_DATA_MAX_BYTES];
    for (uint32_t cursor = 0; cursor < 64; cursor += 8) {
        size_t size = 0;
        CHECK(gs232b_provisioning_scan_page(&provisioning, cursor == 0 ? 0 : UINT32_MAX,
            cursor, bytes, sizeof(bytes), &size) == GS232B_OK);
        CHECK(size < sizeof(bytes));
        gs232b_json_value_t page, next;
        CHECK(gs232b_json_parse((gs232b_byte_span_t){bytes, size}, &page) == GS232B_OK);
        CHECK(has_field(page, "scan_id", UINT32_MAX) && has_field(page, "offset", cursor));
        CHECK(has_field(page, "total", 64));
        CHECK(gs232b_json_get(page, "next", &next) == GS232B_OK);
        CHECK(cursor == 56 ? next.type == GS232B_JSON_NULL : has_field(page, "next", cursor + 8));
        bytes[size] = 0;
        unsigned entries = 0;
        for (char *entry = (char *)bytes; (entry = strstr(entry, "\"ssid\"")) != NULL; ++entry) ++entries;
        CHECK(entries == 8 && strstr((char *)bytes, "\\u001f") != NULL);
    }
    CHECK(fixture.calls == 1);
    return true;
}

int main(void)
{
    if (!test_snapshot_pages_and_expiration() || !test_scan_failure_and_callback_contract() ||
        !test_worst_case_escaped_pages()) return 1;
    puts("Provisioning scan pagination tests passed");
    return 0;
}
