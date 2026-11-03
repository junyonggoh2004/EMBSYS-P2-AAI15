#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t sequence, timestamp_ms, echo_us;
    int acquisition_processor;
    bool distance_valid,aht_valid,light_valid;
    float distance_cm,temperature_c,humidity_pct;
    uint32_t light_lux;
    const char *distance_status,*aht_status,*light_status;
    int aht_i2c_error,light_i2c_error;
} sensor_sample_t;
void sensors_read(sensor_sample_t *sample);
