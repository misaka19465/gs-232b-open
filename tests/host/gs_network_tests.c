#include "network_test_support.h"

#include <inttypes.h>
#include <stdio.h>

bool network_validation_tests(void);
bool network_lifecycle_tests(void);

static gs232b_network_config_t stored_config(const gs232b_net_config_t *config,
    uint32_t confirmed_trial_id)
{
    gs232b_network_config_t stored = {
        .eth = config->eth,
        .enabled = config->wifi.enabled,
        .security = GS232B_NETWORK_SECURITY_WPA2_PERSONAL,
        .ssid_length = (uint8_t)strlen(config->wifi.ssid),
        .password_length = (uint8_t)strlen(config->wifi.password),
        .last_confirmed_trial_id = confirmed_trial_id
    };
    memcpy(stored.ssid, config->wifi.ssid, stored.ssid_length + 1U);
    memcpy(stored.password, config->wifi.password, stored.password_length + 1U);
    return stored;
}

static gs232b_net_config_t runtime_config(const gs232b_network_config_t *stored)
{
    gs232b_net_config_t config = {.eth = stored->eth};
    config.wifi.enabled = stored->enabled;
    memcpy(config.wifi.ssid, stored->ssid, stored->ssid_length + 1U);
    memcpy(config.wifi.password, stored->password, stored->password_length + 1U);
    return config;
}

static bool trial_boot(gs_fixture_t *f, runtime_t *runtime,
    const gs232b_fake_store_t *store_image, const gs232b_net_config_t *active,
    uint64_t revision)
{
    memset(f, 0, sizeof(*f));
    f->store = *store_image;
    gs232b_motion_config_t motion_config;
    gs232b_motion_sim_config(&motion_config);
    CHECK(gs232b_motion_sim_init(&f->sim, &motion_config, 0, WAIT_US) == GS232B_OK);
    CHECK(gs232b_fake_sessions_init(&f->io, f->sim.manager.clock) == GS232B_OK);
    CHECK(gs232b_dispatcher_init(&f->dispatcher, &f->sim.motion,
        gs232b_fake_store_port(&f->store), gs232b_fake_sessions_port(&f->io),
        f->request_bytes, sizeof(f->request_bytes)) == GS232B_OK);
    const gs232b_net_status_port_t status = {.ctx = runtime, .snapshot = runtime_snapshot};
    CHECK(gs232b_dispatcher_network_configure(&f->dispatcher, active, revision, status) == GS232B_OK);
    CHECK(gs232b_dispatcher_network_apply_configure(&f->dispatcher,
        (gs232b_net_apply_port_t){.ctx = runtime, .apply = runtime_apply}) == GS232B_OK);
    runtime->applied_config = *active;
    runtime->eth.state = GS232B_ETHERNET_READY;
    runtime->eth.link_up = true;
    runtime->eth.ipv4_valid = true;
    runtime->eth.current.address = UINT32_C(0xc000020a);
    runtime->eth.current.netmask = UINT32_C(0xffffff00);
    runtime->wifi.state = GS232B_WIFI_STA_READY;
    runtime->wifi.enabled = true;
    runtime->wifi.connected = true;
    runtime->wifi.ipv4_valid = true;
    runtime->wifi.current.address = UINT32_C(0xc6336402);
    for (size_t i = 0; i < GS_TEST_SESSIONS; ++i) {
        CHECK(gs_open(f, i, i == 2 ? GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS :
            (gs232b_transport_t)i));
    }
    return true;
}

static bool setup_trial(gs_fixture_t *f, runtime_t *runtime)
{
    const gs232b_net_config_t active = {
        .eth = {.mode = GS232B_ETHERNET_STATIC,
            .ipv4 = {.address = UINT32_C(0xc0a80a32), .netmask = UINT32_C(0xffffff00),
                .gateway = UINT32_C(0xc0a80a01)}},
        .wifi = {.enabled = true, .ssid = "home", .password = "prior-secret"}
    };
    gs232b_fake_store_t store;
    gs232b_fake_store_init(&store);
    const gs232b_store_t port = gs232b_fake_store_port(&store);
    const gs232b_network_config_t persisted = stored_config(&active, 0);
    uint64_t revision = 0;
    CHECK(gs232b_persistence_network_commit(&port, 0, &persisted, &revision) == GS232B_OK);
    CHECK(revision == 1);
    return trial_boot(f, runtime, &store, &active, revision);
}

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

static bool prepare_trial_candidate(gs_fixture_t *f, size_t session, uint32_t candidate_id)
{
    char expected[64];
    snprintf(expected, sizeof(expected), "\"candidate_id\":%" PRIu32, candidate_id);
    CHECK(response_contains(f, session, "!CFG BEGIN\r", expected));
    CHECK(response_contains(f, session,
        "!NET SET {\"eth\":{\"address\":\"192.168.10.60\"},"
        "\"wifi\":{\"ssid\":\"new-home\",\"password\":\"new-secret\"}}\r",
        "\"ok\":true"));
    return true;
}

static bool apply_trial(gs_fixture_t *f, size_t session, uint32_t candidate_id,
    uint32_t timeout_s, uint32_t trial_id)
{
    char command[128], expected[80];
    snprintf(command, sizeof(command),
        "!CFG APPLY {\"candidate_id\":%" PRIu32 ",\"timeout_s\":%" PRIu32 "}\r",
        candidate_id, timeout_s);
    snprintf(expected, sizeof(expected), "\"trial_id\":%" PRIu32, trial_id);
    return response_contains(f, session, command, expected);
}

static bool confirm_trial(gs_fixture_t *f, size_t session, uint32_t trial_id,
    uint64_t revision)
{
    char command[96], expected[96];
    snprintf(command, sizeof(command), "!CFG CONFIRM {\"trial_id\":%" PRIu32 "}\r", trial_id);
    snprintf(expected, sizeof(expected), "\"active_revision\":%" PRIu64, revision);
    return response_contains(f, session, command, expected);
}

static bool rejected_network(gs_fixture_t *f, size_t session, const char *line,
    gs232b_error_t error)
{
    CHECK(gs_feed(f, session, line) == GS232B_OK);
    (void)gs232b_dispatcher_tick(&f->dispatcher);
    uint8_t bytes[512];
    size_t size = 0;
    CHECK(gs232b_fake_sessions_take_tx(&f->io, f->sessions[session],
        bytes, sizeof(bytes) - 1U, &size) == GS232B_OK);
    bytes[size] = '\0';
    char expected[64];
    snprintf(expected, sizeof(expected), "\"error\":\"%s\"", gs232b_error_name(error));
    if (strstr((const char *)bytes, expected) == NULL) {
        fprintf(stderr, "expected network error [%s], got [%s]\n", expected, bytes);
        CHECK(false);
    }
    return true;
}

static bool test_trial_confirmation_and_session_handoff(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup_trial(&f, &runtime));
    const gs232b_net_config_t active = f.dispatcher.network_config.active;
    CHECK(prepare_trial_candidate(&f, 0, 1));
    CHECK(apply_trial(&f, 0, 1, 30, 1));
    CHECK(f.dispatcher.network_config.trial.state == GS232B_CONFIG_TRIAL_ACTIVE);
    CHECK(f.dispatcher.network_config.active_revision == 1);
    CHECK(strcmp(runtime.applied_config.wifi.ssid, "new-home") == 0);
    CHECK(f.store.records[GS232B_STORE_NETWORK].present);
    gs232b_network_config_t persisted;
    uint64_t revision = 0;
    CHECK(gs232b_persistence_network_load(&f.dispatcher.store, &persisted, &revision) == GS232B_OK);
    CHECK(revision == 1 && persisted.eth.ipv4.address == active.eth.ipv4.address);
    CHECK(strcmp(persisted.ssid, "home") == 0 && persisted.last_confirmed_trial_id == 0);

    const gs232b_session_key_t original = f.sessions[0];
    CHECK(gs232b_dispatcher_session_close(&f.dispatcher, original) == GS232B_OK);
    CHECK(gs_open(&f, 0, GS232B_TRANSPORT_UART1));
    CHECK(f.sessions[0].generation > original.generation);
    CHECK(confirm_trial(&f, 0, 1, 2));
    CHECK(f.dispatcher.network_config.active_revision == 2);
    CHECK(strcmp(f.dispatcher.network_config.active.wifi.ssid, "new-home") == 0);
    CHECK(f.dispatcher.network_config.active.eth.ipv4.address == UINT32_C(0xc0a80a3c));
    CHECK(confirm_trial(&f, 0, 1, 2));
    CHECK(gs232b_persistence_network_load(&f.dispatcher.store, &persisted, &revision) == GS232B_OK);
    CHECK(revision == 2 && persisted.last_confirmed_trial_id == 1);
    CHECK(persisted.eth.ipv4.address == UINT32_C(0xc0a80a3c));
    CHECK(strcmp(persisted.ssid, "new-home") == 0);
    CHECK(strcmp(persisted.password, "new-secret") == 0);
    return true;
}

static bool test_trial_abort_timeout_and_retry(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup_trial(&f, &runtime));
    const gs232b_net_config_t active = f.dispatcher.network_config.active;
    CHECK(prepare_trial_candidate(&f, 0, 1));
    CHECK(apply_trial(&f, 0, 1, 30, 1));
    CHECK(response_contains(&f, 2, "!CFG ABORT {\"trial_id\":1}\r", "\"ok\":true"));
    CHECK(f.dispatcher.network_config.trial.state == GS232B_CONFIG_TRIAL_NONE);
    CHECK(memcmp(&runtime.applied_config, &active, sizeof(active)) == 0);
    CHECK(rejected(&f, 2, "!CFG CONFIRM {\"trial_id\":1}\r", GS232B_ERROR_CONFLICT));

    CHECK(prepare_trial_candidate(&f, 0, 2));
    CHECK(apply_trial(&f, 0, 2, 30, 2));
    runtime.apply_failures_remaining = 1;
    runtime.apply_failure = GS232B_ERROR_IO;
    runtime.mutate_before_apply_failure = true;
    f.sim.now_us += UINT64_C(30000000);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(f.dispatcher.network_config.trial.state == GS232B_CONFIG_TRIAL_ROLLBACK_PENDING);
    CHECK(gs232b_dispatcher_tick(&f.dispatcher) == GS232B_OK);
    CHECK(f.dispatcher.network_config.trial.state == GS232B_CONFIG_TRIAL_NONE);
    CHECK(memcmp(&runtime.applied_config, &active, sizeof(active)) == 0);
    CHECK(rejected(&f, 2, "!CFG CONFIRM {\"trial_id\":2}\r", GS232B_ERROR_CONFLICT));
    return true;
}

static bool test_apply_failure_and_persistence_boundaries(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup_trial(&f, &runtime));
    const gs232b_net_config_t active = f.dispatcher.network_config.active;
    CHECK(prepare_trial_candidate(&f, 0, 1));
    runtime.apply_failures_remaining = 1;
    runtime.apply_failure = GS232B_ERROR_IO;
    runtime.mutate_before_apply_failure = true;
    CHECK(rejected(&f, 0,
        "!CFG APPLY {\"candidate_id\":1,\"timeout_s\":30}\r", GS232B_ERROR_IO));
    CHECK(memcmp(&runtime.applied_config, &active, sizeof(active)) == 0);
    CHECK(f.dispatcher.network_config.trial.state == GS232B_CONFIG_TRIAL_NONE);
    uint32_t last_trial_id = 0;
    CHECK(gs232b_persistence_trial_id_load(&f.dispatcher.store, &last_trial_id) == GS232B_OK);
    CHECK(last_trial_id == 1);

    CHECK(prepare_trial_candidate(&f, 0, 2));
    CHECK(gs232b_fake_fail_next(&f.store.failures[GS232B_STORE_CONFIG_META]
        [GS232B_FAKE_STORE_INTERRUPT], GS232B_ERROR_TIMEOUT, 1) == GS232B_OK);
    CHECK(rejected(&f, 0,
        "!CFG APPLY {\"candidate_id\":2,\"timeout_s\":30}\r", GS232B_ERROR_TIMEOUT));
    CHECK(f.dispatcher.network_config.trial.state == GS232B_CONFIG_TRIAL_NONE);
    CHECK(memcmp(&runtime.applied_config, &active, sizeof(active)) == 0);

    CHECK(apply_trial(&f, 0, 2, 30, 2));
    CHECK(gs232b_fake_fail_next(&f.store.failures[GS232B_STORE_NETWORK]
        [GS232B_FAKE_STORE_INTERRUPT], GS232B_ERROR_TIMEOUT, 1) == GS232B_OK);
    CHECK(rejected(&f, 2, "!CFG CONFIRM {\"trial_id\":2}\r", GS232B_ERROR_TIMEOUT));
    CHECK(f.dispatcher.network_config.active_revision == 1);
    CHECK(f.dispatcher.network_config.trial.state == GS232B_CONFIG_TRIAL_NONE);
    CHECK(memcmp(&runtime.applied_config, &active, sizeof(active)) == 0);
    gs232b_network_config_t persisted;
    uint64_t revision = 0;
    CHECK(gs232b_persistence_network_load(&f.dispatcher.store, &persisted, &revision) == GS232B_OK);
    CHECK(revision == 1 && persisted.eth.ipv4.address == active.eth.ipv4.address);
    CHECK(strcmp(persisted.ssid, "home") == 0);
    return true;
}

static bool test_motion_sequence_calibration_gate(void)
{
    gs_fixture_t f;
    runtime_t runtime = {0};
    CHECK(setup_trial(&f, &runtime));
    CHECK(prepare_trial_candidate(&f, 0, 1));
    CHECK(gs232b_motion_manual(&f.sim.motion, GS232B_AXIS_AZIMUTH,
        GS232B_MOTION_POSITIVE) == GS232B_OK);
    CHECK(rejected_network(&f, 0,
        "!CFG APPLY {\"candidate_id\":1,\"timeout_s\":30}\r", GS232B_ERROR_BUSY));
    CHECK(gs232b_motion_stop(&f.sim.motion, GS232B_MOTION_ALL_AXES) == GS232B_OK);
    f.dispatcher.control.sequence_state.state = GS232B_SEQUENCE_LOADED_WAIT_T;
    CHECK(rejected_network(&f, 0,
        "!CFG APPLY {\"candidate_id\":1,\"timeout_s\":30}\r", GS232B_ERROR_BUSY));
    f.dispatcher.control.sequence_state = (gs232b_sequence_snapshot_t){0};
    f.dispatcher.control.sequence.active.point_count = 0;
    f.dispatcher.control.calibration.kind = GS232B_INTERACTION_ZERO_AZ;
    f.dispatcher.control.input_reservation.session = f.sessions[0];
    CHECK(rejected_network(&f, 0,
        "!CFG APPLY {\"candidate_id\":1,\"timeout_s\":30}\r", GS232B_ERROR_BUSY));
    f.dispatcher.control.calibration = (gs232b_calibration_t){0};
    f.dispatcher.control.input_reservation = (gs232b_command_context_t){0};
    CHECK(rejected_network(&f, 0,
        "!CFG APPLY {\"candidate_id\":1,\"timeout_s\":29}\r", GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(rejected_network(&f, 0,
        "!CFG APPLY {\"candidate_id\":1,\"timeout_s\":601}\r", GS232B_ERROR_INVALID_ARGUMENT));
    CHECK(f.dispatcher.network_config.trial.state == GS232B_CONFIG_TRIAL_NONE);
    return true;
}

static bool test_restart_discards_trial_and_rejects_old_id(void)
{
    gs_fixture_t f, restarted;
    runtime_t runtime = {0};
    CHECK(setup_trial(&f, &runtime));
    CHECK(prepare_trial_candidate(&f, 0, 1));
    CHECK(apply_trial(&f, 0, 1, 60, 1));
    const gs232b_fake_store_t image = f.store;
    gs232b_network_config_t persisted;
    uint64_t revision = 0;
    CHECK(gs232b_persistence_network_load(&f.dispatcher.store, &persisted, &revision) == GS232B_OK);
    const gs232b_net_config_t active = runtime_config(&persisted);
    CHECK(trial_boot(&restarted, &runtime, &image, &active, revision));
    CHECK(restarted.dispatcher.network_config.last_trial_id == 1);
    CHECK(restarted.dispatcher.network_config.trial.state == GS232B_CONFIG_TRIAL_NONE);
    CHECK(strcmp(restarted.dispatcher.network_config.active.wifi.ssid, "home") == 0);
    CHECK(rejected(&restarted, 0, "!CFG CONFIRM {\"trial_id\":1}\r", GS232B_ERROR_CONFLICT));
    CHECK(prepare_trial_candidate(&restarted, 0, 1));
    CHECK(apply_trial(&restarted, 0, 1, 30, 2));
    CHECK(rejected(&restarted, 2, "!CFG CONFIRM {\"trial_id\":1}\r", GS232B_ERROR_CONFLICT));
    CHECK(response_contains(&restarted, 1, "!CFG STATUS\r", "\"trial_id\":2"));
    return true;
}

int main(void)
{
    if (!test_candidate_validation_and_isolation() || !test_close_and_idle_expiration() ||
        !test_trial_confirmation_and_session_handoff() ||
        !test_trial_abort_timeout_and_retry() ||
        !test_apply_failure_and_persistence_boundaries() ||
        !test_motion_sequence_calibration_gate() ||
        !test_restart_discards_trial_and_rejects_old_id() ||
        !network_validation_tests() || !network_lifecycle_tests()) return 1;
    return 0;
}
