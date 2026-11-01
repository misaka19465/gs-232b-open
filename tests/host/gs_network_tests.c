#include "network_test_support.h"

bool network_validation_tests(void);
bool network_lifecycle_tests(void);

static bool test_candidate_validation_and_isolation(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    const gs232b_net_config_t active = f.dispatcher.network_config.active;
    CHECK(response_contains(&f, 1, "!NET GET\r", "\"active_revision\":7"));
    CHECK(response_contains(&f, 1, "!NET GET\r", "\"state\":\"ready\""));
    CHECK(gs232b_dispatcher_network_configure(&f.dispatcher, &active, 7, (gs232b_net_status_port_t){0}) == GS232B_ERROR_BUSY);
    CHECK(rejected(&f, 1, "!CFG BEGIN\r", GS232B_ERROR_NOT_AUTHORIZED));
    CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":1"));
    CHECK(rejected(&f, 2, "!CFG BEGIN\r", GS232B_ERROR_CONFLICT));
    CHECK(response_contains(&f, 1, "!CFG STATUS\r", "\"base_revision\":7"));

    const char set[] = "!NET SET {\"eth\":{\"mode\":\"static\",\"address\":\"192.168.10.50\",\"netmask\":\"255.255.255.0\",\"gateway\":\"192.168.10.1\"},\"wifi\":{\"enabled\":true,\"ssid\":\"new-home\",\"password\":\"new-secret\"}}\r";
    CHECK(response_contains(&f, 0, set, "\"ok\":true"));
    CHECK(memcmp(&f.dispatcher.network_config.active, &active, sizeof(active)) == 0);
    CHECK(f.store.records[GS232B_STORE_NETWORK].present == false);
    CHECK(response_contains(&f, 0, "!CFG CHECK\r", "\"eth\":true,\"wifi\":true"));
    CHECK(response_contains(&f, 0, "!NET GET\r", "\"password_set\":true"));
    CHECK(response_contains(&f, 0, "!NET GET\r", "192.0.2.10"));

    const gs232b_net_config_t before = f.dispatcher.network_config.candidate;
    CHECK(rejected(&f, 0, "!NET SET {\"wifi\":{\"password\":\"********\"}}\r",
        GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(rejected(&f, 0, "!NET SET {\"wifi\":{\"password\":\"[REDACTED]\"}}\r",
        GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(rejected(&f, 0, "!NET SET {\"wifi\":{\"password\":\"short\"}}\r",
        GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(rejected(&f, 0, "!NET SET {\"eth\":{\"netmask\":\"255.0.255.0\"}}\r",
        GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(rejected(&f, 0, "!NET SET {\"eth\":{\"address\":\"192.168.10.0\"}}\r",
        GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(rejected(&f, 0, "!NET SET {\"eth\":{\"gateway\":\"192.168.11.1\"}}\r",
        GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(memcmp(&f.dispatcher.network_config.candidate, &before, sizeof(before)) == 0);

    CHECK(response_contains(&f, 0, "!NET SET {\"wifi\":{\"ssid\":\"renamed\"}}\r", "\"ok\":true"));
    CHECK(strcmp(f.dispatcher.network_config.candidate.wifi.password, "new-secret") == 0);
    CHECK(strcmp(f.dispatcher.network_config.candidate.wifi.ssid, "renamed") == 0);
    CHECK(f.dispatcher.network_config.candidate.eth.ipv4.address == UINT32_C(0xc0a80a32));
    CHECK(response_contains(&f, 1, "!NET GET\r", "\"ssid\":\"home\""));
    CHECK(response_contains(&f, 1, "!NET GET\r", "\"ssid\":\"renamed\""));
    CHECK(gs_run(&f, 1, "C\r", "AZ=090\r\n"));
    CHECK(gs_run(&f, 1, "S\r", "\r"));
    CHECK(stopped(&f.sim));
    return true;
}

static bool test_close_and_idle_expiration(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup(&f, &runtime));
    CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":1"));
    const gs232b_session_key_t first = f.sessions[0];
    CHECK(gs232b_dispatcher_session_close(&f.dispatcher, first) == GS232B_OK);
    CHECK(rejected(&f, 2, "!NET SET {\"wifi\":{\"ssid\":\"lost\"}}\r", GS232B_ERROR_NOT_READY));
    CHECK(gs_open(&f, 0, GS232B_TRANSPORT_UART1));
    CHECK(f.sessions[0].generation > first.generation);
    CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":2"));
    f.sim.now_us += GS232B_CONFIG_IDLE_TIMEOUT_US;
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(rejected(&f, 0, "!CFG CHECK\r", GS232B_ERROR_NOT_READY));
    CHECK(response_contains(&f, 0, "!CFG BEGIN\r", "\"candidate_id\":3"));
    return true;
}

int main(void)
{
    if (!test_candidate_validation_and_isolation() || !test_close_and_idle_expiration() ||
        !network_validation_tests() || !network_lifecycle_tests()) return 1;
    return 0;
}
