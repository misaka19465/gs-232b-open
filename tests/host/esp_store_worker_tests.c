#include "gs232b/esp_store_worker.h"
#include "gs232b/motion_sim.h"
#include "gs232b/fakes.h"
#include "ethernet_api.h"
#include "nvs_flash.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return false; } } while (0)
static gs232b_fake_store_t medium;
static pthread_t caller, owner;
static unsigned writes, reads;
static bool wrong_thread;
static esp_err_t init_error;

esp_err_t nvs_flash_init(void)
{
    owner = pthread_self();
    wrong_thread |= pthread_equal(owner, caller);
    return init_error;
}

gs232b_error_t gs232b_nvs_store_open(gs232b_nvs_store_t *store, const char *name)
{
    (void)name;
    wrong_thread |= !pthread_equal(owner, pthread_self());
    store->open = true;
    return GS232B_OK;
}

void gs232b_nvs_store_close(gs232b_nvs_store_t *store)
{
    wrong_thread |= !pthread_equal(owner, pthread_self());
    store->open = false;
}

static gs232b_error_t raw_load(void *ctx, gs232b_store_domain_t domain,
    uint8_t *buffer, size_t capacity, size_t *size)
{
    (void)ctx;
    ++reads;
    wrong_thread |= !pthread_equal(owner, pthread_self());
    const gs232b_store_t raw = gs232b_fake_store_port(&medium);
    return gs232b_store_load(&raw, domain, buffer, capacity, size);
}

static gs232b_error_t raw_previous(void *ctx, gs232b_store_domain_t domain,
    uint8_t *buffer, size_t capacity, size_t *size)
{
    (void)ctx;
    ++reads;
    wrong_thread |= !pthread_equal(owner, pthread_self());
    const gs232b_store_t raw = gs232b_fake_store_port(&medium);
    return gs232b_store_load_previous(&raw, domain, buffer, capacity, size);
}

static gs232b_error_t raw_save(void *ctx, gs232b_store_domain_t domain,
    const uint8_t *bytes, size_t size)
{
    (void)ctx;
    ++writes;
    wrong_thread |= !pthread_equal(owner, pthread_self());
    const gs232b_store_t raw = gs232b_fake_store_port(&medium);
    return gs232b_store_save(&raw, domain, bytes, size);
}

static gs232b_error_t raw_erase(void *ctx, gs232b_store_domain_t domain)
{
    (void)ctx;
    ++writes;
    wrong_thread |= !pthread_equal(owner, pthread_self());
    const gs232b_store_t raw = gs232b_fake_store_port(&medium);
    return gs232b_store_erase(&raw, domain);
}

gs232b_store_t gs232b_nvs_store_port(gs232b_nvs_store_t *store)
{
    return (gs232b_store_t){.ctx = store, .load = raw_load, .load_previous = raw_previous,
        .save = raw_save, .erase = raw_erase};
}

static bool close_worker(gs232b_esp_store_worker_t *worker)
{
    CHECK(gs232b_esp_store_worker_close(worker) == GS232B_OK);
    struct timespec started, current;
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    do {
        sched_yield();
        (void)clock_gettime(CLOCK_MONOTONIC, &current);
    } while (fake_eth_status().live_tasks != 0 && current.tv_sec - started.tv_sec < 2);
    CHECK(fake_eth_status().live_tasks == 0 && fake_eth_status().live_queues == 0);
    return true;
}

static bool worker_contracts(void)
{
    fake_eth_reset();
    gs232b_fake_store_init(&medium);
    caller = pthread_self();
    reads = writes = 0;
    wrong_thread = false;
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    gs232b_motion_sim_t sim;
    CHECK(gs232b_motion_sim_init(&sim, &config, 0, 100) == GS232B_OK);
    gs232b_esp_store_worker_t worker = {0};
    gs232b_control_t control = {0};
    CHECK(gs232b_esp_store_worker_open(&worker, &sim.motion, &sim.manager, &control) == GS232B_OK);
    const gs232b_store_t store = gs232b_esp_store_worker_port(&worker);
    CHECK(reads == 2U * GS232B_STORE_DOMAIN_COUNT && writes == 0 && !wrong_thread);
    const uint8_t old[] = {1, 2, 3}, next[] = {4, 5};
    CHECK(gs232b_store_save(&store, GS232B_STORE_DISPLAY, old, sizeof(old)) == GS232B_OK);
    CHECK(gs232b_store_save(&store, GS232B_STORE_DISPLAY, next, sizeof(next)) == GS232B_OK);
    uint8_t bytes[32] = {0};
    size_t size;
    CHECK(gs232b_store_load(&store, GS232B_STORE_DISPLAY, NULL, 0, &size) == GS232B_ERROR_BUFFER_TOO_SMALL && size == 2);
    CHECK(gs232b_store_load(&store, GS232B_STORE_DISPLAY, bytes, sizeof(bytes), &size) == GS232B_OK &&
        size == sizeof(next) && memcmp(bytes, next, size) == 0);
    CHECK(gs232b_store_load_previous(&store, GS232B_STORE_DISPLAY, bytes, sizeof(bytes), &size) == GS232B_OK &&
        size == sizeof(old) && memcmp(bytes, old, size) == 0);
    CHECK(reads == 2U * GS232B_STORE_DOMAIN_COUNT);
    CHECK(gs232b_motion_manual(&sim.motion, 0, GS232B_MOTION_POSITIVE) == GS232B_OK);
    CHECK(gs232b_store_save(&store, GS232B_STORE_ADMIN, old, sizeof(old)) == GS232B_ERROR_BUSY);
    CHECK(gs232b_store_erase(&store, GS232B_STORE_DISPLAY) == GS232B_ERROR_BUSY && writes == 2);
    CHECK(gs232b_esp_store_worker_close(&worker) == GS232B_ERROR_BUSY);
    CHECK(gs232b_motion_sim_step(&sim, 100) == GS232B_OK && sim.shadow != 0);
    CHECK(gs232b_store_save(&store, GS232B_STORE_ADMIN, old, sizeof(old)) == GS232B_ERROR_BUSY && writes == 2);
    CHECK(gs232b_motion_stop(&sim.motion, GS232B_MOTION_ALL_AXES) == GS232B_OK);
    control.initialized = true;
    control.sequence_state.state = GS232B_SEQUENCE_RUNNING;
    CHECK(gs232b_store_save(&store, GS232B_STORE_ADMIN, old, sizeof(old)) == GS232B_ERROR_BUSY && writes == 2);
    control.sequence_state.state = GS232B_SEQUENCE_EMPTY;
    CHECK(gs232b_fake_fail_next(&medium.failures[GS232B_STORE_DISPLAY][GS232B_FAKE_STORE_SAVE],
        GS232B_ERROR_IO, 1) == GS232B_OK);
    CHECK(gs232b_store_save(&store, GS232B_STORE_DISPLAY, old, sizeof(old)) == GS232B_ERROR_IO);
    CHECK(gs232b_store_load(&store, GS232B_STORE_DISPLAY, bytes, sizeof(bytes), &size) == GS232B_OK && bytes[0] == 4);
    CHECK(gs232b_store_erase(&store, GS232B_STORE_DISPLAY) == GS232B_OK);
    CHECK(gs232b_store_load(&store, GS232B_STORE_DISPLAY, bytes, sizeof(bytes), &size) == GS232B_ERROR_NOT_FOUND);
    CHECK(!wrong_thread && writes == 4);
    worker.current[GS232B_STORE_NETWORK].error = GS232B_ERROR_BUFFER_TOO_SMALL;
    worker.current[GS232B_STORE_NETWORK].size = GS232B_PERSISTENCE_MAX_RECORD_SIZE + 1U;
    CHECK(gs232b_store_load(&store, GS232B_STORE_NETWORK, bytes, sizeof(bytes), &size) == GS232B_ERROR_BUFFER_TOO_SMALL);
    CHECK(size == GS232B_PERSISTENCE_MAX_RECORD_SIZE + 1U);
    CHECK(close_worker(&worker));
    init_error = ESP_FAIL;
    CHECK(gs232b_esp_store_worker_open(&worker, &sim.motion, &sim.manager, &control) == GS232B_ERROR_IO);
    CHECK(gs232b_store_load(&store, GS232B_STORE_DISPLAY, bytes, sizeof(bytes), &size) == GS232B_ERROR_NOT_FOUND);
    CHECK(gs232b_store_save(&store, GS232B_STORE_DISPLAY, old, sizeof(old)) == GS232B_ERROR_IO);
    CHECK(writes == 4 && close_worker(&worker));
    return true;
}

int main(void)
{
    if (!worker_contracts()) return 1;
    puts("Flash owner isolation, stopped-only writes, cache and commit failures passed");
    return 0;
}
