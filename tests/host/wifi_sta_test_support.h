#ifndef WIFI_STA_TEST_SUPPORT_H
#define WIFI_STA_TEST_SUPPORT_H

#include "wifi_api.h"
#include "ethernet_api.h"
#include "esp_timer.h"
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return false; } } while (0)
typedef struct { gs232b_esp_wifi_sta_t sta; } wifi_fixture_t;
static const gs232b_wifi_sta_config_t wifi_good = {
    .enabled = true, .ssid = "测试 network", .password = "correct password"};
static const gs232b_wifi_sta_ipv4_t wifi_ip = {
    .address = 0xc0a80122, .netmask = 0xffffff00, .gateway = 0xc0a80101,
    .dns = {0x08080808, 0x01010101}};

static inline uint64_t wifi_wall_ms(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static inline bool wifi_settle(wifi_fixture_t *f)
{
    const uint64_t deadline = wifi_wall_ms() + 3000;
    while (wifi_wall_ms() < deadline) {
        fake_eth_flush();
        const gs232b_error_t result = gs232b_esp_wifi_sta_service(&f->sta);
        if (result != GS232B_ERROR_WOULD_BLOCK && f->sta.job == 0) { return true; }
        sched_yield();
    }
    CHECK(false);
}

static inline bool wifi_snapshot(wifi_fixture_t *f, gs232b_wifi_sta_snapshot_t *out)
{
    const uint64_t deadline = wifi_wall_ms() + 3000;
    while (wifi_wall_ms() < deadline) {
        const gs232b_error_t result = gs232b_esp_wifi_sta_snapshot(&f->sta, out);
        if (result == GS232B_OK) { return true; }
        CHECK(result == GS232B_ERROR_WOULD_BLOCK);
        sched_yield();
    }
    CHECK(false);
}

static inline bool wifi_open(wifi_fixture_t *f, const gs232b_wifi_sta_config_t *config)
{
    memset(f, 0, sizeof(*f));
    fake_eth_reset();
    fake_wifi_reset();
    CHECK(gs232b_esp_wifi_sta_open(&f->sta, config) == GS232B_OK);
    CHECK(wifi_settle(f));
    return true;
}

static inline gs232b_error_t wifi_close_result(wifi_fixture_t *f)
{
    const uint64_t deadline = wifi_wall_ms() + 3000;
    while (wifi_wall_ms() < deadline) {
        fake_eth_flush();
        const gs232b_error_t result = gs232b_esp_wifi_sta_close(&f->sta);
        if (result != GS232B_ERROR_WOULD_BLOCK) { return result; }
        sched_yield();
    }
    return GS232B_ERROR_TIMEOUT;
}

static inline bool wifi_close(wifi_fixture_t *f)
{
    CHECK(wifi_close_result(f) == GS232B_OK);
    const uint64_t deadline = wifi_wall_ms() + 3000;
    while (fake_eth_status().live_tasks != 0 && wifi_wall_ms() < deadline) { sched_yield(); }
    fake_eth_flush();
    const fake_eth_status_t resources = fake_eth_status();
    CHECK(resources.live_handlers == 0 && resources.live_netifs == 0 && resources.live_tasks == 0);
    CHECK(resources.live_semaphores == 0 && resources.live_queues == 0);
    CHECK(!fake_wifi_status().initialized && !fake_wifi_status().started);
    return true;
}

static inline void wifi_ready(wifi_fixture_t *f)
{
    fake_wifi_connected();
    fake_wifi_lease(&f->sta, wifi_ip);
}

bool wifi_test_faults(void);
bool wifi_test_coexistence(void);
#endif
