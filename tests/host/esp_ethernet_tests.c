#include "ethernet_api.h"
#include "gs232b/spi_bus.h"

#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); return false; \
} } while (0)

typedef struct {
    gs232b_fake_spi_bus_t spi;
    gs232b_shared_spi_t bus;
    gs232b_output_manager_t outputs;
    gs232b_esp_ethernet_t eth;
} fixture_t;

static const gs232b_ethernet_config_t static_config = {
    .mode = GS232B_ETHERNET_STATIC,
    .ipv4 = {.address = 0xc0a80102, .netmask = 0xffffff00, .gateway = 0xc0a80101,
             .dns = {0x08080808, 0x01010101}}
};

static uint64_t wall_ms(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static bool fixture_init(fixture_t *f, const gs232b_ethernet_config_t *config)
{
    fake_eth_reset();
    memset(f, 0, sizeof(*f));
    CHECK(gs232b_fake_spi_bus_init(&f->spi, &f->bus));
    CHECK(gs232b_output_manager_init(&f->outputs,
        (gs232b_clock_t){&f->spi, gs232b_fake_spi_bus_now},
        gs232b_shared_spi_output_driver(&f->bus), 100) == GS232B_OK);
    CHECK(gs232b_esp_ethernet_open(&f->eth, &f->bus, &f->outputs, config) == GS232B_OK);
    return true;
}

static void advance(fixture_t *f, uint64_t us)
{
    atomic_fetch_add(&f->spi.now_us, us);
    fake_eth_advance(us);
}

static bool settle(fixture_t *f, bool success)
{
    const uint64_t deadline = wall_ms() + 3000;
    while (wall_ms() < deadline) {
        fake_eth_flush();
        const gs232b_error_t result = gs232b_esp_ethernet_service(&f->eth);
        if (f->eth.reset.phase == GS232B_W5500_READY ||
            f->eth.reset.phase == GS232B_W5500_FAILED) {
            fake_eth_flush();
            if (result == GS232B_ERROR_WOULD_BLOCK) { sched_yield(); continue; }
            CHECK(success ? f->eth.reset.phase == GS232B_W5500_READY && result == GS232B_OK :
                            f->eth.reset.phase == GS232B_W5500_FAILED && result != GS232B_OK);
            return true;
        }
        if (f->eth.reset.phase == GS232B_W5500_RESET_LOW) { advance(f, 1000); }
        if (f->eth.reset.phase == GS232B_W5500_RECOVERING) { advance(f, 10000); }
        sched_yield();
    }
    CHECK(false);
}

static bool snapshot(fixture_t *f, gs232b_ethernet_snapshot_t *out)
{
    const uint64_t deadline = wall_ms() + 3000;
    gs232b_error_t result;
    do {
        (void)gs232b_esp_ethernet_service(&f->eth);
        result = gs232b_esp_ethernet_snapshot(&f->eth, out);
        if (result == GS232B_ERROR_WOULD_BLOCK) { sched_yield(); }
    } while (result == GS232B_ERROR_WOULD_BLOCK && wall_ms() < deadline);
    CHECK(result == GS232B_OK);
    return true;
}

static bool fixture_close(fixture_t *f)
{
    const uint64_t deadline = wall_ms() + 3000;
    gs232b_error_t result = GS232B_ERROR_WOULD_BLOCK;
    while (wall_ms() < deadline && result == GS232B_ERROR_WOULD_BLOCK) {
        fake_eth_flush();
        result = gs232b_esp_ethernet_close(&f->eth);
        sched_yield();
    }
    CHECK(result == GS232B_OK);
    fake_eth_flush();
    while (wall_ms() < deadline && fake_eth_status().live_tasks != 0) { sched_yield(); }
    const fake_eth_status_t s = fake_eth_status();
    CHECK(s.live_drivers == 0 && s.live_macs == 0 && s.live_phys == 0 && s.live_glues == 0);
    CHECK(s.live_handlers == 0 && s.live_netifs == 0 && s.live_tasks == 0);
    CHECK(s.live_semaphores == 0 && s.live_queues == 0 && !s.invariant_failed);
    CHECK(!atomic_load(&f->bus.ethernet_enabled) && atomic_load(&f->spi.safe));
    gs232b_fake_spi_bus_destroy(&f->spi);
    return true;
}

static bool test_ipv4_validation(void)
{
    const char *invalid[] = {"", "192.168.1", "1.2.3.4.5", "1..2.3", "1.2.3.256",
        "-1.2.3.4", "1.2.3.4 ", " 1.2.3.4", "0x1.2.3.4", "01.2.3.4",
        "1.2.3.000", "999999999999999999.1.1.1"};
    uint32_t ip = 42;
    CHECK(gs232b_ethernet_parse_ipv4("192.168.1.2", &ip) == GS232B_OK && ip == 0xc0a80102);
    CHECK(gs232b_ethernet_parse_ipv4("0.0.0.0", &ip) == GS232B_OK && ip == 0);
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(gs232b_ethernet_parse_ipv4(invalid[i], &ip) == GS232B_ERROR_INVALID_ARGUMENT);
        CHECK(ip == 0);
    }
    CHECK(gs232b_ethernet_parse_ipv4(NULL, &ip) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_ethernet_parse_ipv4("1.2.3.4", NULL) == GS232B_ERROR_INVALID_ARGUMENT);
    CHECK(gs232b_ethernet_config_valid(&static_config));
    gs232b_ethernet_config_t candidate = static_config;
    candidate.ipv4.gateway = 0; candidate.ipv4.dns[0] = 0; candidate.ipv4.dns[1] = 0;
    CHECK(gs232b_ethernet_config_valid(&candidate));
    const uint32_t bad_mask[] = {0, 0xffffffff, 0xfffffffe, 0xffff00ff, 0x00ffffff};
    for (unsigned i = 0; i < sizeof(bad_mask) / sizeof(bad_mask[0]); ++i) {
        candidate = static_config; candidate.ipv4.netmask = bad_mask[i];
        CHECK(!gs232b_ethernet_config_valid(&candidate));
    }
    const uint32_t bad_ip[] = {0, 0xc0a80100, 0xc0a801ff, 0x7f000001, 0xe0000001, 0xffffffff};
    for (unsigned i = 0; i < sizeof(bad_ip) / sizeof(bad_ip[0]); ++i) {
        candidate = static_config; candidate.ipv4.address = bad_ip[i];
        CHECK(!gs232b_ethernet_config_valid(&candidate));
    }
    const uint32_t bad_gateway[] = {0xc0a80201, 0xc0a801ff, 0xc0a80100, 0xc0a80102};
    for (unsigned i = 0; i < sizeof(bad_gateway) / sizeof(bad_gateway[0]); ++i) {
        candidate = static_config; candidate.ipv4.gateway = bad_gateway[i];
        CHECK(!gs232b_ethernet_config_valid(&candidate));
    }
    candidate = static_config; candidate.ipv4.dns[0] = 0xe0000001;
    CHECK(!gs232b_ethernet_config_valid(&candidate));
    candidate = static_config; candidate.mode = GS232B_ETHERNET_DHCP;
    CHECK(!gs232b_ethernet_config_valid(&candidate));
    candidate = (gs232b_ethernet_config_t){0};
    CHECK(gs232b_ethernet_config_valid(&candidate));
    candidate.mode = (gs232b_ethernet_mode_t)9;
    CHECK(!gs232b_ethernet_config_valid(&candidate));
    return true;
}

static bool test_dhcp_events_and_sessions(void)
{
    fixture_t f;
    CHECK(fixture_init(&f, NULL));
    gs232b_ethernet_snapshot_t s;
    CHECK(snapshot(&f, &s));
    CHECK(s.state == GS232B_ETHERNET_LINK_DOWN && !s.link_up && !s.ipv4_valid);
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK);
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_ERROR_BUSY);
    CHECK(settle(&f, true));
    CHECK(snapshot(&f, &s));
    CHECK(!s.dhcp_running && !s.ipv4_valid && s.session_epoch == 1);
    fake_eth_link(&f.eth, true);
    CHECK(snapshot(&f, &s));
    CHECK(s.link_up && !s.ipv4_valid && s.current.address == 0 && s.dhcp_running);
    CHECK(s.state == GS232B_ETHERNET_DHCP_WAIT);
    CHECK(fake_eth_dhcp(&f.eth) == ESP_NETIF_DHCP_STARTED);
    fake_eth_lease(&f.eth, static_config.ipv4);
    CHECK(snapshot(&f, &s));
    CHECK(s.state == GS232B_ETHERNET_READY && s.ipv4_valid && s.current.address == 0xc0a80102);
    CHECK(s.configured.ipv4.address == 0 && s.current.dns[1] == 0x01010101);
    const uint64_t first = s.session_epoch;
    fake_eth_lease(&f.eth, static_config.ipv4);
    CHECK(snapshot(&f, &s) && s.session_epoch == first);
    gs232b_ethernet_ipv4_t changed = static_config.ipv4;
    changed.dns[0] = 0x09090909;
    fake_eth_lease(&f.eth, changed);
    CHECK(snapshot(&f, &s) && s.session_epoch == first && s.current.dns[0] == 0x09090909);
    changed.address++;
    fake_eth_lease(&f.eth, changed);
    CHECK(snapshot(&f, &s) && s.session_epoch == first + 1 && s.current.address == changed.address);
    changed.netmask = 0xffff0000;
    fake_eth_lease(&f.eth, changed);
    CHECK(snapshot(&f, &s) && s.session_epoch == first + 2);
    changed.gateway = 0xc0a801fe;
    fake_eth_lease(&f.eth, changed);
    CHECK(snapshot(&f, &s) && s.session_epoch == first + 3);

    const ip_event_got_ip_t stale_loss = {.esp_netif = f.eth.netif};
    CHECK(esp_event_post(IP_EVENT, IP_EVENT_ETH_LOST_IP, &stale_loss, sizeof(stale_loss), 0) == ESP_OK);
    esp_eth_handle_t foreign = (esp_eth_handle_t)(uintptr_t)0x1234;
    CHECK(esp_event_post(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &foreign, sizeof(foreign), 0) == ESP_OK);
    const ip_event_got_ip_t foreign_ip = {.esp_netif = (esp_netif_t *)(uintptr_t)0x1234};
    CHECK(esp_event_post(IP_EVENT, IP_EVENT_ETH_LOST_IP, &foreign_ip, sizeof(foreign_ip), 0) == ESP_OK);
    fake_eth_flush();
    CHECK(snapshot(&f, &s) && s.ipv4_valid && s.session_epoch == first + 3);
    fake_eth_lost_ip(&f.eth);
    CHECK(snapshot(&f, &s) && !s.ipv4_valid && s.link_up && s.current.address == 0);
    CHECK(s.session_epoch == first + 4 && s.state == GS232B_ETHERNET_DHCP_WAIT);
    fake_eth_lease(&f.eth, changed);
    CHECK(snapshot(&f, &s) && s.ipv4_valid);
    fake_eth_link(&f.eth, false);
    CHECK(snapshot(&f, &s) && !s.link_up && !s.ipv4_valid && !s.dhcp_running);
    CHECK(s.session_epoch == first + 5 && s.state == GS232B_ETHERNET_LINK_DOWN);
    fake_eth_link(&f.eth, true);
    CHECK(snapshot(&f, &s) && s.state == GS232B_ETHERNET_DHCP_WAIT && !s.ipv4_valid);
    fake_eth_lease(&f.eth, changed);
    CHECK(snapshot(&f, &s) && s.ipv4_valid && s.session_epoch == first + 5);
    CHECK(fixture_close(&f));
    return true;
}

static bool test_modes_and_stable_mac(void)
{
    fixture_t f;
    CHECK(fixture_init(&f, &static_config));
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
    CHECK(fake_eth_dhcp(&f.eth) == ESP_NETIF_DHCP_STOPPED);
    CHECK(fake_eth_status().calls[ETH_FAKE_DHCP_START] == 0);
    CHECK(fake_eth_status().route_prio == GS232B_ESP_ETHERNET_ROUTE_PRIORITY);
    gs232b_ethernet_snapshot_t s;
    CHECK(snapshot(&f, &s) && !s.ipv4_valid && !s.dhcp_running);
    fake_eth_link(&f.eth, true);
    CHECK(snapshot(&f, &s) && s.ipv4_valid && s.state == GS232B_ETHERNET_READY);
    CHECK(s.current.dns[0] == static_config.ipv4.dns[0]);
    uint8_t mac[6]; memcpy(mac, s.mac, sizeof(mac));
    const fake_eth_status_t first = fake_eth_status();
    CHECK(memcmp(mac, first.official_mac, 6) == 0 && memcmp(mac, first.installed_mac, 6) == 0);
    fake_eth_link(&f.eth, false);
    fake_eth_link(&f.eth, true);
    CHECK(snapshot(&f, &s) && s.ipv4_valid && !s.dhcp_running);

    const gs232b_ethernet_config_t dhcp = {0};
    CHECK(gs232b_esp_ethernet_configure(&f.eth, &dhcp) == GS232B_OK);
    CHECK(snapshot(&f, &s) && !s.ipv4_valid && s.current.address == 0);
    CHECK(gs232b_esp_ethernet_configure(&f.eth, &static_config) == GS232B_ERROR_BUSY);
    CHECK(settle(&f, true));
    gs232b_ethernet_ipv4_t runtime = fake_eth_runtime(&f.eth);
    CHECK(runtime.address == 0 && runtime.dns[0] == 0 && runtime.dns[1] == 0);
    fake_eth_link(&f.eth, true);
    CHECK(fake_eth_dhcp(&f.eth) == ESP_NETIF_DHCP_STARTED);
    fake_eth_lease(&f.eth, static_config.ipv4);
    CHECK(snapshot(&f, &s) && s.configured.ipv4.address == 0);
    CHECK(memcmp(s.mac, mac, 6) == 0);

    gs232b_ethernet_config_t direct = static_config;
    direct.ipv4.gateway = 0; direct.ipv4.dns[0] = 0; direct.ipv4.dns[1] = 0;
    CHECK(gs232b_esp_ethernet_configure(&f.eth, &direct) == GS232B_OK && settle(&f, true));
    fake_eth_link(&f.eth, true);
    CHECK(snapshot(&f, &s) && s.ipv4_valid && !s.dhcp_running);
    CHECK(s.current.gateway == 0 && s.current.dns[0] == 0 && s.current.dns[1] == 0);
    CHECK(memcmp(s.mac, mac, 6) == 0);
    CHECK(fake_eth_status().calls[ETH_FAKE_READ_MAC] == 1);
    CHECK(fixture_close(&f));

    CHECK(fixture_init(&f, NULL));
    CHECK(snapshot(&f, &s) && memcmp(s.mac, mac, 6) == 0);
    CHECK(fixture_close(&f));
    return true;
}

static bool test_dhcp_failure_and_late_lease(void)
{
    fixture_t f;
    CHECK(fixture_init(&f, NULL));
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
    fake_eth_link(&f.eth, true);
    advance(&f, GS232B_ETHERNET_DHCP_TIMEOUT_US - 1);
    CHECK(gs232b_esp_ethernet_service(&f.eth) == GS232B_OK);
    advance(&f, 1);
    CHECK(gs232b_esp_ethernet_service(&f.eth) == GS232B_ERROR_TIMEOUT);
    gs232b_ethernet_snapshot_t s;
    CHECK(snapshot(&f, &s) && s.state == GS232B_ETHERNET_ERROR && !s.ipv4_valid);
    CHECK(s.configured.mode == GS232B_ETHERNET_DHCP && s.current.address == 0);
    CHECK(s.link_up && s.dhcp_running);
    fake_eth_lease(&f.eth, static_config.ipv4);
    CHECK(snapshot(&f, &s) && s.ipv4_valid && s.last_error == GS232B_OK);
    fake_eth_lost_ip(&f.eth);
    advance(&f, GS232B_ETHERNET_DHCP_TIMEOUT_US);
    CHECK(gs232b_esp_ethernet_service(&f.eth) == GS232B_ERROR_TIMEOUT);
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
    fake_eth_fail(ETH_FAKE_DHCP_START, 0, ESP_ERR_ESP_NETIF_DHCPC_START_FAILED);
    fake_eth_link(&f.eth, true);
    CHECK(snapshot(&f, &s) && s.state == GS232B_ETHERNET_LINK_UP_NO_IP && !s.dhcp_running);
    advance(&f, GS232B_ETHERNET_DHCP_TIMEOUT_US);
    CHECK(gs232b_esp_ethernet_service(&f.eth) == GS232B_ERROR_TIMEOUT);
    fake_eth_link(&f.eth, false);
    fake_eth_link(&f.eth, true);
    CHECK(snapshot(&f, &s) && s.state == GS232B_ETHERNET_DHCP_WAIT);
    fake_eth_lease(&f.eth, static_config.ipv4);
    CHECK(snapshot(&f, &s) && s.ipv4_valid);
    CHECK(fixture_close(&f));
    return true;
}

static bool test_recovery_preserves_outputs_and_stop(void)
{
    fixture_t f;
    CHECK(fixture_init(&f, NULL));
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
    CHECK(gs232b_output_manager_set_pwm(&f.outputs, 630) == GS232B_OK);
    CHECK(gs232b_output_manager_set_directions(&f.outputs, GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    CHECK(f.spi.latched == 0x15 && f.spi.pwm == 630);
    fake_eth_link(&f.eth, true);
    fake_eth_lease(&f.eth, static_config.ipv4);
    gs232b_ethernet_snapshot_t s;
    CHECK(snapshot(&f, &s));
    const uint64_t epoch = s.session_epoch;
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK);
    CHECK(snapshot(&f, &s) && !s.ipv4_valid && s.session_epoch == epoch + 1);
    CHECK(settle(&f, true) && f.spi.latched == 0x15 && f.spi.pwm == 630);
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK);
    CHECK(gs232b_output_manager_stop_all(&f.outputs) == GS232B_OK);
    CHECK(settle(&f, true) && f.spi.latched == 0x10 && f.spi.pwm == 0);
    CHECK(gs232b_output_manager_set_directions(&f.outputs, GS232B_AZ_CW, GS232B_EL_UP) == GS232B_OK);
    CHECK(gs232b_output_manager_set_directions(&f.outputs, GS232B_AZ_CCW, GS232B_EL_UP) == GS232B_OK);
    CHECK(f.spi.latched == 0x11);
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
    CHECK(f.spi.latched == 0x11 && f.spi.pwm == 0);
    CHECK(f.outputs.state.reversal_waiting[GS232B_AXIS_AZIMUTH]);
    CHECK(fixture_close(&f));
    return true;
}

static bool test_install_and_configuration_failures(void)
{
    const fake_eth_call_t fail_at[] = {ETH_FAKE_FACTORY, ETH_FAKE_INSTALL, ETH_FAKE_SET_MAC,
        ETH_FAKE_NETIF, ETH_FAKE_GLUE, ETH_FAKE_ATTACH, ETH_FAKE_DHCP_STOP,
        ETH_FAKE_SET_IP, ETH_FAKE_SET_DNS, ETH_FAKE_REGISTER, ETH_FAKE_START};
    for (unsigned i = 0; i < sizeof(fail_at) / sizeof(fail_at[0]); ++i) {
        fixture_t f;
        CHECK(fixture_init(&f, &static_config));
        fake_eth_fail(fail_at[i], fail_at[i] == ETH_FAKE_REGISTER ? 1 : 0, ESP_FAIL);
        CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, false));
        gs232b_ethernet_snapshot_t s;
        CHECK(snapshot(&f, &s) && s.state == GS232B_ETHERNET_ERROR && !s.ipv4_valid);
        CHECK(!atomic_load(&f.bus.ethernet_enabled));
        CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
        fake_eth_link(&f.eth, true);
        CHECK(snapshot(&f, &s) && s.ipv4_valid && !s.dhcp_running);
        CHECK(fixture_close(&f));
    }
    return true;
}

static bool test_teardown_failure_and_output_fault(void)
{
    const fake_eth_call_t fail_at[] = {ETH_FAKE_STOP, ETH_FAKE_UNREGISTER, ETH_FAKE_FENCE,
        ETH_FAKE_GLUE_DELETE, ETH_FAKE_UNINSTALL, ETH_FAKE_PHY_DELETE, ETH_FAKE_MAC_DELETE};
    for (unsigned i = 0; i < sizeof(fail_at) / sizeof(fail_at[0]); ++i) {
        fixture_t f;
        CHECK(fixture_init(&f, NULL));
        CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
        fake_eth_fail(fail_at[i], 0, ESP_ERR_TIMEOUT);
        const uint8_t latched = f.spi.latched;
        CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, false));
        CHECK(f.spi.latched == latched && f.outputs.state.output.known);
        CHECK(!atomic_load(&f.bus.ethernet_enabled));
        CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
        CHECK(fixture_close(&f));
    }

    fixture_t f;
    CHECK(fixture_init(&f, NULL));
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK);
    gs232b_fake_spi_bus_fault(&f.spi, SPI_SHIFT_BYTE, 0, GS232B_ERROR_IO);
    CHECK(settle(&f, false));
    CHECK(!f.outputs.state.output.known && f.outputs.state.output.last_error == GS232B_ERROR_IO);
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_ERROR_NOT_READY);
    CHECK(gs232b_output_manager_stop_all(&f.outputs) == GS232B_OK);
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
    CHECK(fixture_close(&f));
    return true;
}

static bool test_open_failures_and_nonblocking_control(void)
{
    for (unsigned allocation = 1; allocation <= 6; ++allocation) {
        fixture_t f;
        fake_eth_reset(); memset(&f, 0, sizeof(f));
        CHECK(gs232b_fake_spi_bus_init(&f.spi, &f.bus));
        CHECK(gs232b_output_manager_init(&f.outputs,
            (gs232b_clock_t){&f.spi, gs232b_fake_spi_bus_now},
            gs232b_shared_spi_output_driver(&f.bus), 100) == GS232B_OK);
        fake_eth_fail_allocation(allocation, allocation == 6);
        CHECK(gs232b_esp_ethernet_open(&f.eth, &f.bus, &f.outputs, NULL) == GS232B_ERROR_RESOURCE_EXHAUSTED);
        CHECK(fake_eth_status().live_queues == 0 && fake_eth_status().live_handlers == 0);
        CHECK(fake_eth_status().live_semaphores == 0 && f.spi.latched == 0);
        CHECK(gs232b_esp_ethernet_close(&f.eth) == GS232B_OK);
        gs232b_fake_spi_bus_destroy(&f.spi);
    }
    fixture_t f;
    CHECK(fixture_init(&f, NULL));
    CHECK(xSemaphoreTake(f.eth.lock, 0) == pdPASS);
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(gs232b_esp_ethernet_configure(&f.eth, &static_config) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(gs232b_esp_ethernet_service(&f.eth) == GS232B_ERROR_WOULD_BLOCK);
    gs232b_ethernet_snapshot_t s;
    CHECK(gs232b_esp_ethernet_snapshot(&f.eth, &s) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(gs232b_esp_ethernet_close(&f.eth) == GS232B_ERROR_WOULD_BLOCK);
    CHECK(xSemaphoreGive(f.eth.lock) == pdPASS);
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK);
    CHECK(fixture_close(&f));
    return true;
}

static bool test_late_worker_completion_after_output_fault(void)
{
    const fake_eth_call_t phases[] = {ETH_FAKE_STOP, ETH_FAKE_START};
    for (unsigned i = 0; i < 2; ++i) {
        fixture_t f;
        CHECK(fixture_init(&f, NULL));
        CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
        fake_eth_pause(phases[i]);
        CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK);
        const uint64_t deadline = wall_ms() + 3000;
        while (wall_ms() < deadline && !fake_eth_paused()) {
            fake_eth_flush();
            const gs232b_error_t result = gs232b_esp_ethernet_service(&f.eth);
            CHECK(result == GS232B_OK || result == GS232B_ERROR_WOULD_BLOCK);
            advance(&f, 11000);
            sched_yield();
        }
        CHECK(fake_eth_paused());
        gs232b_fake_spi_bus_fault(&f.spi, SPI_SHIFT_BYTE, 0, GS232B_ERROR_IO);
        CHECK(gs232b_output_manager_stop_all(&f.outputs) == GS232B_ERROR_IO);
        CHECK(gs232b_esp_ethernet_service(&f.eth) == GS232B_ERROR_NOT_READY);
        CHECK(f.eth.reset.phase == GS232B_W5500_FAILED);
        fake_eth_release();
        while (wall_ms() < deadline && f.eth.job != 0) {
            fake_eth_flush();
            (void)gs232b_esp_ethernet_service(&f.eth);
            sched_yield();
        }
        CHECK(f.eth.job == 0);
        gs232b_ethernet_snapshot_t s;
        CHECK(snapshot(&f, &s) && !s.ipv4_valid && s.state == GS232B_ETHERNET_ERROR);
        CHECK(f.outputs.state.output.last_error == GS232B_ERROR_IO && !f.outputs.state.output.known);
        CHECK(gs232b_output_manager_stop_all(&f.outputs) == GS232B_OK);
        CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
        CHECK(fixture_close(&f));
    }
    return true;
}

static bool test_session_epoch_exhaustion(void)
{
    fixture_t f;
    CHECK(fixture_init(&f, NULL));
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
    fake_eth_link(&f.eth, true);
    fake_eth_lease(&f.eth, static_config.ipv4);
    (void)gs232b_esp_ethernet_service(&f.eth);
    CHECK(xSemaphoreTake(f.eth.lock, 0) == pdPASS);
    f.eth.state.session_epoch = UINT64_MAX - 1;
    CHECK(xSemaphoreGive(f.eth.lock) == pdPASS);
    gs232b_ethernet_ipv4_t changed = static_config.ipv4;
    ++changed.address;
    fake_eth_lease(&f.eth, changed);
    gs232b_ethernet_snapshot_t s;
    CHECK(snapshot(&f, &s) && s.session_epoch == UINT64_MAX && !s.ipv4_valid);
    CHECK(s.state == GS232B_ETHERNET_ERROR && s.last_error == GS232B_ERROR_RESOURCE_EXHAUSTED);
    fake_eth_link(&f.eth, false); fake_eth_link(&f.eth, true);
    fake_eth_lease(&f.eth, changed);
    CHECK(snapshot(&f, &s) && !s.ipv4_valid && s.state == GS232B_ETHERNET_ERROR);
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(gs232b_esp_ethernet_configure(&f.eth, &static_config) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(fixture_close(&f));
    return true;
}

static bool test_event_read_failure_and_close_retry(void)
{
    const fake_eth_call_t failures[] = {ETH_FAKE_GET_IP, ETH_FAKE_GET_DNS};
    for (unsigned i = 0; i < 2; ++i) {
        fixture_t f;
        CHECK(fixture_init(&f, NULL));
        CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
        fake_eth_link(&f.eth, true);
        fake_eth_lease(&f.eth, static_config.ipv4);
        gs232b_ethernet_snapshot_t s;
        CHECK(snapshot(&f, &s));
        const uint64_t epoch = s.session_epoch;
        fake_eth_fail(failures[i], 0, ESP_FAIL);
        fake_eth_lease(&f.eth, static_config.ipv4);
        CHECK(snapshot(&f, &s) && s.state == GS232B_ETHERNET_ERROR && !s.ipv4_valid);
        CHECK(s.session_epoch == epoch + 1 && s.last_error == GS232B_ERROR_IO);
        CHECK(fixture_close(&f));
    }

    fixture_t f;
    CHECK(fixture_init(&f, NULL));
    const uint64_t deadline = wall_ms() + 3000;
    /* NOTE: 尚未 recover，唯一 unregister 是最后释放屏障 handler。 */
    fake_eth_fail(ETH_FAKE_UNREGISTER, 0, ESP_ERR_TIMEOUT);
    gs232b_error_t result = GS232B_ERROR_WOULD_BLOCK;
    while (wall_ms() < deadline && result == GS232B_ERROR_WOULD_BLOCK) {
        fake_eth_flush();
        result = gs232b_esp_ethernet_close(&f.eth);
        sched_yield();
    }
    CHECK(result == GS232B_ERROR_TIMEOUT && f.eth.closing == 4);
    CHECK(fixture_close(&f));
    return true;
}

static bool test_callbacks_only_enqueue_and_overflow_invalidates(void)
{
    fixture_t f;
    CHECK(fixture_init(&f, NULL));
    CHECK(gs232b_esp_ethernet_recover(&f.eth) == GS232B_OK && settle(&f, true));
    CHECK(xSemaphoreTake(f.eth.lock, 0) == pdPASS);
    fake_eth_link(&f.eth, true);
    fake_eth_lease(&f.eth, static_config.ipv4);
    CHECK(!f.eth.state.link_up && !f.eth.state.ipv4_valid);
    CHECK(uxQueueMessagesWaiting(f.eth.events) == 2);
    CHECK(xSemaphoreGive(f.eth.lock) == pdPASS);
    gs232b_ethernet_snapshot_t state;
    CHECK(snapshot(&f, &state) && state.ipv4_valid);
    const uint64_t epoch = state.session_epoch;
    const unsigned reads = fake_eth_status().calls[ETH_FAKE_GET_IP];
    CHECK(xSemaphoreTake(f.eth.lock, 0) == pdPASS);
    for (unsigned i = 0; i <= GS232B_NETWORK_EVENT_CAPACITY; ++i) fake_eth_link(&f.eth, true);
    CHECK(uxQueueMessagesWaiting(f.eth.events) == GS232B_NETWORK_EVENT_CAPACITY);
    CHECK(atomic_load(&f.eth.event_lost));
    CHECK(f.eth.state.ipv4_valid && fake_eth_status().calls[ETH_FAKE_GET_IP] == reads);
    CHECK(xSemaphoreGive(f.eth.lock) == pdPASS);
    CHECK(gs232b_esp_ethernet_service(&f.eth) == GS232B_ERROR_RESOURCE_EXHAUSTED);
    CHECK(gs232b_esp_ethernet_snapshot(&f.eth, &state) == GS232B_OK);
    CHECK(state.state == GS232B_ETHERNET_ERROR && !state.ipv4_valid && state.session_epoch > epoch);
    for (unsigned i = 0; i < 4; ++i) (void)gs232b_esp_ethernet_service(&f.eth);
    CHECK(gs232b_esp_ethernet_snapshot(&f.eth, &state) == GS232B_OK && !state.ipv4_valid);
    CHECK(fixture_close(&f));
    return true;
}

static gs232b_error_t boot_lifecycle(void *ctx)
{
    (void)ctx;
    return GS232B_OK;
}

static bool test_adopts_completed_startup_reset(void)
{
    fake_eth_reset();
    fixture_t f = {0};
    CHECK(gs232b_fake_spi_bus_init(&f.spi, &f.bus));
    CHECK(gs232b_output_manager_init(&f.outputs,
        (gs232b_clock_t){&f.spi, gs232b_fake_spi_bus_now},
        gs232b_shared_spi_output_driver(&f.bus), 100) == GS232B_OK);
    gs232b_w5500_reset_t startup;
    CHECK(gs232b_w5500_reset_init(&startup, &f.outputs, &f.bus,
        (gs232b_w5500_lifecycle_t){NULL, boot_lifecycle, boot_lifecycle}, 1000, 10000) == GS232B_OK);
    CHECK(gs232b_w5500_reset_begin(&startup) == GS232B_OK);
    CHECK(gs232b_esp_ethernet_open_after_reset(&f.eth, &startup, NULL) == GS232B_ERROR_NOT_READY);
    advance(&f, 1000);
    CHECK(gs232b_w5500_reset_tick(&startup) == GS232B_OK);
    advance(&f, 10000);
    CHECK(gs232b_w5500_reset_tick(&startup) == GS232B_OK && startup.phase == GS232B_W5500_READY);
    gs232b_fake_spi_bus_clear(&f.spi);
    CHECK(gs232b_esp_ethernet_open_after_reset(&f.eth, &startup, NULL) == GS232B_OK);
    CHECK(settle(&f, true));
    CHECK(fake_eth_status().calls[ETH_FAKE_INSTALL] == 1);
    CHECK(f.spi.latched == 0x10 && f.spi.pwm == 0);
    for (size_t i = 0; i < f.spi.count; ++i)
        CHECK(f.spi.trace[i].kind != SPI_LATCH_HIGH && f.spi.trace[i].kind != SPI_SHIFT_BYTE);
    CHECK(fixture_close(&f));
    return true;
}

int main(void)
{
    if (!test_ipv4_validation() || !test_dhcp_events_and_sessions() ||
        !test_modes_and_stable_mac() || !test_dhcp_failure_and_late_lease() ||
        !test_recovery_preserves_outputs_and_stop() ||
        !test_install_and_configuration_failures() ||
        !test_teardown_failure_and_output_fault() ||
        !test_open_failures_and_nonblocking_control() ||
        !test_late_worker_completion_after_output_fault() ||
        !test_session_epoch_exhaustion() ||
        !test_event_read_failure_and_close_retry() ||
        !test_callbacks_only_enqueue_and_overflow_invalidates() ||
        !test_adopts_completed_startup_reset()) { return 1; }
    puts("W5500 Ethernet lifecycle, lease, static/DHCP and recovery tests passed");
    return 0;
}
