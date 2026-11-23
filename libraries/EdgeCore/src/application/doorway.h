#ifndef APPLICATION_DOORWAY_H
#define APPLICATION_DOORWAY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DOORWAY_NONE, DOORWAY_ENTRY, DOORWAY_EXIT, DOORWAY_INVALID, DOORWAY_EXPIRED,
    DOORWAY_AMBIGUOUS
} doorway_result_t;

typedef enum {
    DOORWAY_UNKNOWN, DOORWAY_CLEAR, DOORWAY_OCCUPIED
} doorway_zone_t;

typedef enum {
    DOORWAY_WAIT_CLEAR, DOORWAY_ARMED, DOORWAY_LEAD, DOORWAY_OVERLAP, DOORWAY_TRAIL
} doorway_phase_t;

typedef struct {
    doorway_zone_t stable, candidate;
    unsigned consecutive;
} doorway_sensor_t;

typedef struct {
    float near_cm[2], clear_cm[2];
    uint64_t crossing_us;
    doorway_sensor_t zone[2];
    doorway_phase_t phase;
    unsigned first;
    unsigned phase_samples;
    uint64_t started_us;
    bool degraded;
} doorway_t;

void doorway_rearm(doorway_t *state);
doorway_result_t doorway_sample(doorway_t *state, unsigned sensor, uint64_t now_us,
                               bool valid, float distance_cm);

#endif
