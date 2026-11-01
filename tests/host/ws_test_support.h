#ifndef GS232B_WS_TEST_SUPPORT_H
#define GS232B_WS_TEST_SUPPORT_H

#include "gs232b/esp_ws.h"
#include "gs232b/fakes.h"
#include "motion_test_support.h"
#include "ws_api.h"
#include <string.h>

#define WS_ETH_IP UINT32_C(0xc0a80102)
#define WS_STA_IP UINT32_C(0xc0a80202)
#define WS_ETH_IF_NAME "en0"
#define WS_STA_IF_NAME "st0"

typedef struct {
    gs232b_motion_sim_t sim;
    gs232b_fake_store_t store;
    gs232b_dispatcher_t dispatcher;
    gs232b_esp_ws_t ws;
    uint8_t requests[8192];
} ws_fixture_t;

static inline void ws_control_step(void *ctx)
{
    gs232b_error_t result = gs232b_esp_ws_service(ctx);
    if (result != GS232B_OK && result != GS232B_ERROR_WOULD_BLOCK) { abort(); }
}

static inline bool ws_init(ws_fixture_t *f)
{
    fake_ws_reset();
    memset(f, 0, sizeof(*f));
    gs232b_motion_config_t config;
    gs232b_motion_sim_config(&config);
    CHECK(gs232b_motion_sim_init(&f->sim, &config, WAIT_US, WAIT_US) == GS232B_OK);
    fake_ws.now_us = f->sim.now_us;
    gs232b_fake_store_init(&f->store);
    CHECK(gs232b_dispatcher_init(&f->dispatcher, &f->sim.motion,
        gs232b_fake_store_port(&f->store), gs232b_esp_ws_io(&f->ws),
        f->requests, sizeof(f->requests)) == GS232B_OK);
    CHECK(gs232b_esp_ws_open(&f->ws, &f->dispatcher) == GS232B_OK);
    CHECK(gs232b_esp_ws_interface(&f->ws, GS232B_TRANSPORT_ETHERNET_WS,
        WS_ETH_IP, 1, WS_ETH_IF_NAME) == GS232B_OK);
    CHECK(gs232b_esp_ws_interface(&f->ws, GS232B_TRANSPORT_WIFI_WS,
        WS_STA_IP, 1, WS_STA_IF_NAME) == GS232B_OK);
    fake_ws.control_step = ws_control_step;
    fake_ws.control_ctx = &f->ws;
    return true;
}

static inline gs232b_ws_client_t *ws_client(ws_fixture_t *f, int fd)
{
    for (size_t i = 0; i < GS232B_WS_CLIENTS; ++i) {
        if (atomic_load(&f->ws.clients[i].occupied) && f->ws.clients[i].fd == fd) {
            return &f->ws.clients[i];
        }
    }
    return NULL;
}

static inline bool ws_poll(ws_fixture_t *f, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        CHECK(gs232b_esp_ws_service(&f->ws) == GS232B_OK);
        CHECK(gs232b_dispatcher_tick(&f->dispatcher) == GS232B_OK);
        fake_ws_run_work();
    }
    return true;
}

static inline bool ws_expect(int fd, httpd_ws_type_t type, const void *bytes, size_t size)
{
    uint8_t received[GS232B_WS_CHUNK_BYTES];
    size_t actual;
    httpd_ws_type_t opcode;
    CHECK(fake_ws_take_frame(fd, &opcode, received, sizeof(received), &actual));
    CHECK(type == opcode && actual == size && memcmp(bytes, received, size) == 0);
    return true;
}

static inline bool ws_expect_bytes(int fd, const char *expected)
{
    uint8_t bytes[4096];
    size_t used = 0, size;
    httpd_ws_type_t type;
    while (fake_ws_take_frame(fd, &type, bytes + used, sizeof(bytes) - used, &size)) {
        CHECK(type == HTTPD_WS_TYPE_BINARY);
        used += size;
    }
    if (used != strlen(expected) || memcmp(bytes, expected, used) != 0) {
        fprintf(stderr, "fd %d: got [%.*s] (%zu), expected [%s]\n", fd, (int)used, bytes, used, expected);
        CHECK(false);
    }
    CHECK(fake_ws.sockets[fd].output_size == 0);
    return true;
}

static inline bool ws_connect(ws_fixture_t *f, int fd, uint32_t ip)
{
    CHECK(fake_ws_connect(fd, ip) == ESP_OK);
    fake_ws_run_work();
    CHECK(ws_expect_bytes(fd, "Connect OK\r\n"));
    CHECK(ws_client(f, fd) != NULL);
    return true;
}

static inline bool ws_command(ws_fixture_t *f, int fd, const char *command, const char *expected)
{
    CHECK(fake_ws_frame(fd, HTTPD_WS_TYPE_BINARY, true, command, strlen(command)) == ESP_OK);
    CHECK(ws_poll(f, 8));
    return ws_expect_bytes(fd, expected);
}

static inline bool ws_close(ws_fixture_t *f)
{
    CHECK(gs232b_esp_ws_close(&f->ws) == GS232B_OK);
    CHECK(fake_ws.live_semaphores == 0 && !fake_ws.started);
    return true;
}
#endif
