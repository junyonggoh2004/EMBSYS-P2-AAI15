#include "application/doorway.h"
#include <assert.h>
#include <stdio.h>

static doorway_t state;
static uint64_t now;
static unsigned entries, exits, invalid, expired, ambiguous;

static void reset(void)
{
    state = (doorway_t){.near_cm = {70, 75}, .clear_cm = {85, 90},
                        .crossing_us = 2000000};
    doorway_rearm(&state);
    now = entries = exits = invalid = expired = ambiguous = 0;
}

static void sample(unsigned sensor, float distance)
{
    now += 70000;
    switch (doorway_sample(&state, sensor, now, distance >= 0, distance)) {
    case DOORWAY_ENTRY: ++entries; break;
    case DOORWAY_EXIT: ++exits; break;
    case DOORWAY_INVALID: ++invalid; break;
    case DOORWAY_EXPIRED: ++expired; break;
    case DOORWAY_AMBIGUOUS: ++ambiguous; break;
    case DOORWAY_NONE: break;
    }
}

static void pair(float a, float b, unsigned repetitions)
{
    for (unsigned i = 0; i < repetitions; ++i) {
        sample(0, a);
        sample(1, b);
    }
}

int main(void)
{
    reset();
    pair(120, 110, 2);
    assert(state.phase == DOORWAY_ARMED);
    pair(70, 110, 2);
    pair(78, 75, 2); // A remains occupied through the hysteresis band
    pair(85, 75, 2);
    assert(entries == 0 && exits == 0);
    pair(85, 90, 8);
    assert(entries == 1 && exits == 0);
    pair(120, 55, 2);
    pair(55, 55, 2);
    pair(55, 110, 2);
    pair(120, 110, 2);
    assert(entries == 1 && exits == 1);

    reset();
    pair(120, 110, 2);
    pair(55, 110, 1); // an isolated near echo
    pair(120, 110, 2);
    assert(state.phase == DOORWAY_ARMED);
    pair(55, 110, 24); // standing in front of A
    assert(expired == 1 && entries == 0 && exits == 0);
    pair(120, 110, 2);
    pair(55, 110, 2);
    pair(55, 55, 2);
    pair(55, 110, 2); // withdrew from B first
    pair(120, 110, 2);
    assert(entries == 0 && exits == 0);

    reset();
    pair(120, 110, 2);
    pair(55, 110, 2);
    pair(55, -1, 4); // missing echo during a possible crossing
    pair(-1, -1, 3);
    pair(55, 55, 2);
    pair(120, 55, 2);
    assert(invalid == 1 && entries == 0 && exits == 0);
    pair(120, 110, 2);
    assert(state.phase == DOORWAY_ARMED);
    pair(55, 55, 2); // both interrupted together
    pair(120, 110, 2);
    assert(ambiguous == 1 && entries == 0 && exits == 0);
    pair(55, 110, 2);
    pair(55, 55, 2);
    pair(120, 55, 2);
    pair(120, 110, 2);
    assert(entries == 1 && exits == 0);
    puts("doorway: PASS (entry, exit, debounce, loitering, turnaround, missing echo, ambiguity, recovery)");
    return 0;
}
