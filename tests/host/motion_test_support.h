#ifndef GS232B_MOTION_TEST_SUPPORT_H
#define GS232B_MOTION_TEST_SUPPORT_H

#include "gs232b/motion_sim.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

#define STEP_US UINT64_C(20000)
#define WAIT_US UINT64_C(200000)

static inline bool snapshot(gs232b_motion_sim_t *sim, gs232b_motion_snapshot_t *state)
{
    CHECK(gs232b_motion_snapshot(&sim->motion, state) == GS232B_OK);
    CHECK(!sim->unsafe_output);
    return true;
}

static inline bool stopped(gs232b_motion_sim_t *sim)
{
    gs232b_motion_snapshot_t state;
    CHECK(snapshot(sim, &state));
    CHECK(state.output.known);
    CHECK(state.output.last_successful.azimuth == GS232B_AZ_STOP);
    CHECK(state.output.last_successful.elevation == GS232B_EL_STOP);
    CHECK(state.output.last_successful.speed_permille == 0);
    CHECK((sim->shadow & 0x0fU) == 0 && sim->pwm_permille == 0);
    return true;
}

static inline bool advance_ok(gs232b_motion_sim_t *sim, size_t steps)
{
    for (size_t i = 0; i < steps; ++i) {
        CHECK(gs232b_motion_sim_step(sim, STEP_US) == GS232B_OK);
        CHECK(!sim->unsafe_output);
    }
    return true;
}

static inline bool run_to_idle(gs232b_motion_sim_t *sim)
{
    for (size_t i = 0; i < 4000; ++i) {
        CHECK(gs232b_motion_sim_step(sim, STEP_US) == GS232B_OK);
        gs232b_motion_snapshot_t state;
        CHECK(snapshot(sim, &state));
        if (state.axes[0].mode == GS232B_AXIS_IDLE &&
            state.axes[1].mode == GS232B_AXIS_IDLE) {
            return stopped(sim);
        }
    }
    CHECK(false);
}

#endif
