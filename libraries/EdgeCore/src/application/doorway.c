#include "application/doorway.h"
#include <string.h>

void doorway_rearm(doorway_t *state)
{
    memset(state->zone, 0, sizeof state->zone);
    state->phase = DOORWAY_WAIT_CLEAR;
    state->degraded = false;
}

static void observe(doorway_t *state, unsigned sensor, float cm)
{
    doorway_sensor_t *zone = &state->zone[sensor];
    doorway_zone_t observation = DOORWAY_UNKNOWN;
    if (cm <= state->near_cm[sensor]) observation = DOORWAY_OCCUPIED;
    else if (cm >= state->clear_cm[sensor]) observation = DOORWAY_CLEAR;

    if (observation == DOORWAY_UNKNOWN || observation == zone->stable) {
        zone->consecutive = 0;
        return;
    }
    if (observation != zone->candidate) {
        zone->candidate = observation;
        zone->consecutive = 0;
    }
    if (++zone->consecutive >= 2) {
        zone->stable = observation;
        zone->consecutive = 0;
    }
}

doorway_result_t doorway_sample(doorway_t *state, unsigned sensor, uint64_t now_us,
                               bool valid, float distance_cm)
{
    if (!valid || !(distance_cm >= 2.0f && distance_cm <= 400.0f)) {
        if (state->degraded) {
            memset(&state->zone[sensor], 0, sizeof state->zone[sensor]);
            return DOORWAY_NONE;
        }
        doorway_rearm(state);
        state->degraded = true;
        return DOORWAY_INVALID;
    }

    if (state->phase >= DOORWAY_LEAD && now_us - state->started_us > state->crossing_us) {
        doorway_rearm(state);
        return DOORWAY_EXPIRED;
    }
    observe(state, sensor, distance_cm);
    if (state->phase_samples < 2) ++state->phase_samples;
    doorway_zone_t a = state->zone[0].stable;
    doorway_zone_t b = state->zone[1].stable;
    bool both_clear = a == DOORWAY_CLEAR && b == DOORWAY_CLEAR;
    doorway_zone_t lead = state->zone[state->first].stable;
    doorway_zone_t trail = state->zone[1u - state->first].stable;

    switch (state->phase) {
    case DOORWAY_WAIT_CLEAR:
        if (both_clear) {
            state->phase = DOORWAY_ARMED;
            state->degraded = false;
        }
        break;
    case DOORWAY_ARMED:
        if (!both_clear) {
            state->first = a == DOORWAY_OCCUPIED ? 0u : 1u;
            state->started_us = now_us;
            state->phase_samples = 0;
            state->phase = DOORWAY_LEAD;
        }
        break;
    case DOORWAY_LEAD:
        if (lead == DOORWAY_CLEAR) state->phase = DOORWAY_WAIT_CLEAR;
        else if (trail == DOORWAY_OCCUPIED) {
            /* With alternating pings, adjacent confirmations cannot establish
               whether both zones changed together between observations. */
            if (state->phase_samples == 1) {
                doorway_rearm(state);
                return DOORWAY_AMBIGUOUS;
            }
            state->phase = DOORWAY_OVERLAP;
        }
        break;
    case DOORWAY_OVERLAP:
        if (trail == DOORWAY_CLEAR) state->phase = DOORWAY_WAIT_CLEAR;
        else if (lead == DOORWAY_CLEAR) {
            state->phase_samples = 0;
            state->phase = DOORWAY_TRAIL;
        }
        break;
    case DOORWAY_TRAIL:
        if (both_clear) {
            if (state->phase_samples == 1) {
                doorway_rearm(state);
                return DOORWAY_AMBIGUOUS;
            }
            state->phase = DOORWAY_ARMED;
            return state->first == 0 ? DOORWAY_ENTRY : DOORWAY_EXIT;
        }
        if (lead == DOORWAY_OCCUPIED) state->phase = DOORWAY_WAIT_CLEAR;
        break;
    }
    return DOORWAY_NONE;
}
