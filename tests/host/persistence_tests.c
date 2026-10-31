#include "gs232b/fakes.h"
#include "gs232b/feedback.h"
#include "gs232b/persistence.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static gs232b_feedback_reading_t reading(uint32_t adc_uv)
{
    return (gs232b_feedback_reading_t){
        .acquired_at_us = 100,
        .adc_uv = adc_uv,
        .valid = true
    };
}

static bool test_versions_and_domain_isolation(void)
{
    gs232b_fake_store_t fake;
    gs232b_fake_store_init(&fake);
    const gs232b_store_t store = gs232b_fake_store_port(&fake);

    gs232b_azimuth_display_settings_t display;
    gs232b_azimuth_display_settings_init(&display);
    uint64_t display_revision = 0;
    CHECK(gs232b_persistence_display_commit(&store, 0, &display, &display_revision) ==
          GS232B_OK);
    CHECK(display_revision == 1);
    display.mode = GS232B_AZIMUTH_DISPLAY_P36;
    uint64_t next_revision = 0;
    CHECK(gs232b_persistence_display_commit(&store, display_revision, &display,
                                             &next_revision) == GS232B_OK);
    CHECK(next_revision == 2);
    display_revision = next_revision;

    gs232b_network_config_t network = {
        .enabled = true,
        .security = GS232B_NETWORK_SECURITY_WPA2_PERSONAL,
        .ssid_length = 4,
        .ssid = "test",
        .password_length = 8,
        .password = "password"
    };
    uint64_t network_revision = 0;
    CHECK(gs232b_persistence_network_commit(&store, 0, &network, &network_revision) ==
          GS232B_OK);
    CHECK(network_revision == 1);

    const gs232b_feedback_reading_t zero = reading(100000);
    CHECK(gs232b_feedback_zero_save(&store, GS232B_AXIS_AZIMUTH, &zero, 100) == GS232B_OK);

    gs232b_azimuth_display_settings_t loaded_display;
    uint64_t loaded_revision = 0;
    CHECK(gs232b_persistence_display_load(&store, &loaded_display, &loaded_revision) ==
          GS232B_OK);
    CHECK(loaded_revision == display_revision &&
          loaded_display.mode == GS232B_AZIMUTH_DISPLAY_P36);
    gs232b_network_config_t loaded_network;
    CHECK(gs232b_persistence_network_load(&store, &loaded_network, &loaded_revision) ==
          GS232B_OK);
    CHECK(loaded_network.ssid_length == 4 &&
          memcmp(loaded_network.ssid, "test", 4) == 0);

    display.mode = GS232B_AZIMUTH_DISPLAY_P45;
    CHECK(gs232b_persistence_display_commit(&store, 1, &display, &next_revision) ==
          GS232B_ERROR_CONFLICT);
    uint32_t zero_uv = 0;
    CHECK(gs232b_feedback_zero_load(&store, GS232B_AXIS_AZIMUTH, &zero_uv) == GS232B_OK);
    CHECK(zero_uv == 100000);
    return true;
}

static bool test_failures_and_recovery(void)
{
    gs232b_fake_store_t fake;
    gs232b_fake_store_init(&fake);
    const gs232b_store_t store = gs232b_fake_store_port(&fake);
    gs232b_network_config_t first = {
        .enabled = true,
        .security = GS232B_NETWORK_SECURITY_WPA2_PERSONAL,
        .ssid_length = 5,
        .ssid = "first",
        .password_length = 8,
        .password = "password"
    };
    uint64_t revision = 0;
    CHECK(gs232b_persistence_network_commit(&store, 0, &first, &revision) == GS232B_OK);

    gs232b_network_config_t second = first;
    second.ssid_length = 6;
    memcpy(second.ssid, "second", 7);
    CHECK(gs232b_fake_fail_next(&fake.failures[GS232B_STORE_NETWORK][GS232B_FAKE_STORE_SAVE],
                                GS232B_ERROR_IO, 1) == GS232B_OK);
    uint64_t failed_revision = 99;
    CHECK(gs232b_persistence_network_commit(&store, revision, &second,
                                             &failed_revision) == GS232B_ERROR_IO);
    CHECK(failed_revision == 0);
    gs232b_network_config_t loaded;
    uint64_t loaded_revision = 0;
    CHECK(gs232b_persistence_network_load(&store, &loaded, &loaded_revision) == GS232B_OK);
    CHECK(loaded_revision == revision && loaded.ssid_length == first.ssid_length);

    CHECK(gs232b_fake_fail_next(
              &fake.failures[GS232B_STORE_NETWORK][GS232B_FAKE_STORE_INTERRUPT],
              GS232B_ERROR_TIMEOUT, 1) == GS232B_OK);
    CHECK(gs232b_persistence_network_commit(&store, revision, &second,
                                             &failed_revision) == GS232B_ERROR_TIMEOUT);
    CHECK(gs232b_persistence_network_load(&store, &loaded, &loaded_revision) == GS232B_OK);
    CHECK(loaded_revision == revision && loaded.ssid_length == first.ssid_length);

    CHECK(gs232b_persistence_network_commit(&store, revision, &second, &revision) ==
          GS232B_OK);
    CHECK(revision == 2);
    fake.records[GS232B_STORE_NETWORK].bytes[0] ^= UINT8_C(0x01);
    CHECK(gs232b_persistence_network_load(&store, &loaded, &loaded_revision) == GS232B_OK);
    CHECK(loaded_revision == 1 && loaded.ssid_length == first.ssid_length);
    return true;
}

int main(void)
{
    if (!test_versions_and_domain_isolation() || !test_failures_and_recovery()) {
        return 1;
    }
    puts("persistence tests passed");
    return 0;
}
