#include "gs232b/provisioning.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "provisioning check failed: %s:%d\n", __FILE__, __LINE__); return 1; } } while (0)

typedef struct { uint64_t now; unsigned starts, stops; } fixture_t;
static gs232b_time_us_t clock_now(void *ctx) { return ((fixture_t *)ctx)->now; }
static gs232b_error_t start_ap(void *ctx, const gs232b_provisioning_ap_config_t *config)
{
    fixture_t *f = ctx; ++f->starts;
    CHECK(config->address != 0 && config->password[0] != '\0');
    return GS232B_OK;
}
static gs232b_error_t stop_ap(void *ctx) { ++((fixture_t *)ctx)->stops; return GS232B_OK; }

int main(void)
{
    fixture_t f = {0};
    gs232b_provisioning_t p;
    const gs232b_net_config_t empty = {0};
    const gs232b_provisioning_ap_port_t ap = {.ctx = &f, .start = start_ap, .stop = stop_ap};
    CHECK(gs232b_provisioning_init(&p, (gs232b_clock_t){&f, clock_now}, &empty, 0, false,
        (gs232b_net_status_port_t){0}, ap, (const uint8_t[]){2, 0, 0, 0, 0, 1}) == GS232B_OK);
    gs232b_provisioning_status_t s;
    CHECK(gs232b_provisioning_snapshot(&p, &s) == GS232B_OK &&
        s.state == GS232B_PROVISIONING_ACTIVE && s.first_boot &&
        s.remaining_us == GS232B_PROVISIONING_DEFAULT_WINDOW_SECONDS * UINT64_C(1000000) &&
        f.starts == 1);
    CHECK(gs232b_provisioning_set_trial_active(&p, true) == GS232B_OK);
    CHECK(gs232b_provisioning_stop(&p) == GS232B_OK && f.stops == 1);
    CHECK(gs232b_provisioning_snapshot(&p, &s) == GS232B_OK &&
        s.state == GS232B_PROVISIONING_OFF && s.recovery_available);
    const gs232b_net_config_t configured = {.wifi = {.enabled = true, .ssid = "home", .password = "password"}};
    fixture_t g = {0};
    CHECK(gs232b_provisioning_init(&p, (gs232b_clock_t){&g, clock_now}, &configured, 1, true,
        (gs232b_net_status_port_t){0}, ap, NULL) == GS232B_OK);
    CHECK(gs232b_provisioning_snapshot(&p, &s) == GS232B_OK &&
        s.state == GS232B_PROVISIONING_OFF && g.starts == 0);
    CHECK(gs232b_provisioning_subnet_overlaps(0xc0a80144, 0xffffff00,
        0xc0a80101, 0xffffff00));
    CHECK(!gs232b_provisioning_subnet_overlaps(0xc0a80144, 0xffffff00,
        0xc0a80201, 0xffffff00));
    puts("Provisioning lifecycle tests passed");
    return 0;
}
