#include "network_test_support.h"

static bool test_wifi_address_transaction(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    const gs232b_ethernet_config_t ethernet = f.dispatcher.network_config.active.eth;
    CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":1"));
    CHECK(response_contains(&f, 0,
        "!NET SET {\"wifi\":{\"mode\":\"static\"}}\r", "\"ok\":true"));
    CHECK(rejected(&f, 0, "!CFG CHECK\r", GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(response_contains(&f, 0,
        "!NET SET {\"wifi\":{\"address\":\"192.168.20.50\",\"netmask\":\"255.255.255.0\"}}\r",
        "\"ok\":true"));
    CHECK(response_contains(&f, 0, "!CFG CHECK\r", "\"eth\":false,\"wifi\":true"));
    CHECK(response_contains(&f, 0, "!NET GET\r", "\"mode\":\"static\",\"address\":\"192.168.20.50\""));
    const gs232b_config_transaction_t before = f.dispatcher.network_config;
    const char *const invalid[] = {
        "{\"wifi\":{\"mode\":\"auto\"}}",
        "{\"wifi\":{\"address\":\"192.168.020.50\"}}",
        "{\"wifi\":{\"address\":\"192.168.20.0\"}}",
        "{\"wifi\":{\"netmask\":\"255.255.255.254\"}}",
        "{\"wifi\":{\"gateway\":\"192.168.21.1\"}}",
        "{\"wifi\":{\"dns1\":\"192.168.20.1\"}}",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        char line[192];
        snprintf(line, sizeof(line), "!NET SET %s\r", invalid[i]);
        CHECK(rejected(&f, 0, line, GS232B_ERROR_INVALID_ARGUMENT));
        CHECK(memcmp(&f.dispatcher.network_config, &before, sizeof(before)) == 0);
    }
    CHECK(response_contains(&f, 0, "!CFG APPLY {\"candidate_id\":1,\"timeout_s\":60}\r",
        "\"trial_id\":1"));
    CHECK(runtime.applied_config.wifi.mode == GS232B_WIFI_STA_STATIC);
    CHECK(runtime.applied_config.wifi.ipv4.address == UINT32_C(0xc0a81432));
    CHECK(runtime.applied_config.wifi.ipv4.gateway == 0);
    CHECK(memcmp(&runtime.applied_config.eth, &ethernet, sizeof(ethernet)) == 0);
    CHECK(response_contains(&f, 0, "!CFG ABORT {\"trial_id\":1}\r", "\"ok\":true"));
    CHECK(runtime.applied_config.wifi.mode == GS232B_WIFI_STA_DHCP);
    CHECK(runtime.applied_config.wifi.ipv4.address == 0);
    CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":2"));
    CHECK(response_contains(&f, 0,
        "!NET SET {\"wifi\":{\"ssid\":\"open network\",\"password\":\"\"}}\r", "\"ok\":true"));
    CHECK(response_contains(&f, 0, "!CFG CHECK\r", "\"valid\":true"));
    CHECK(response_contains(&f, 0, "!NET GET\r", "\"security\":\"open\",\"password_set\":false"));
    return true;
}

static bool test_network_schema_roundtrip(void)
{
    gs232b_fake_store_t fake;
    gs232b_fake_store_init(&fake);
    const gs232b_store_t store = gs232b_fake_store_port(&fake);
    const gs232b_network_config_t config = {
        .enabled = true, .security = GS232B_NETWORK_SECURITY_OPEN,
        .ssid_length = 4, .ssid = "open", .last_confirmed_trial_id = 7,
        .wifi_mode = GS232B_WIFI_STA_STATIC,
        .wifi_ipv4 = {UINT32_C(0xc0a81432), UINT32_C(0xffffff00), 0},
    };
    uint64_t revision;
    CHECK(gs232b_persistence_network_commit(&store, 0, &config, &revision) == GS232B_OK);
    gs232b_network_config_t loaded;
    CHECK(gs232b_persistence_network_load(&store, &loaded, &revision) == GS232B_OK);
    CHECK(revision == 1 && loaded.last_confirmed_trial_id == 7);
    CHECK(loaded.security == GS232B_NETWORK_SECURITY_OPEN && loaded.password_length == 0);
    CHECK(loaded.wifi_mode == GS232B_WIFI_STA_STATIC);
    CHECK(memcmp(&loaded.wifi_ipv4, &config.wifi_ipv4, sizeof(config.wifi_ipv4)) == 0);
    gs232b_persisted_record_t record;
    CHECK(gs232b_persistence_load(&store, GS232B_STORE_NETWORK, &record) == GS232B_OK);
    CHECK(record.schema_version == GS232B_PERSISTENCE_SCHEMA_NETWORK);
    for (uint16_t schema = 1; schema <= 2; ++schema) {
        gs232b_fake_store_t old;
        gs232b_fake_store_init(&old);
        const gs232b_store_t legacy = gs232b_fake_store_port(&old);
        const size_t offset = schema == 1 ? 21U : 0U;
        const size_t suffix = schema == 1 ? 17U : 13U;
        CHECK(gs232b_persistence_commit(&legacy, GS232B_STORE_NETWORK, 0, schema,
            record.payload + offset, record.payload_size - offset - suffix, &revision) == GS232B_OK);
        CHECK(gs232b_persistence_network_load(&legacy, &loaded, &revision) == GS232B_OK);
        CHECK(loaded.wifi_mode == GS232B_WIFI_STA_DHCP && loaded.wifi_ipv4.address == 0);
        CHECK(loaded.security == GS232B_NETWORK_SECURITY_OPEN && strcmp(loaded.ssid, "open") == 0);
        CHECK(loaded.last_confirmed_trial_id == (schema == 1 ? 0U : 7U));
    }
    return true;
}

bool network_ipv4_tests(void)
{
    return test_wifi_address_transaction() && test_network_schema_roundtrip();
}
