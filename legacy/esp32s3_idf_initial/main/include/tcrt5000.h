#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_status.h"
#include "device_config.h"
#include "hardware_config.h"

typedef struct {
    app_status_t status;
    bool detected;
    int raw_level;
} tcrt5000_digital_result_t;

typedef struct {
    app_status_t status;
    int raw;
    int millivolts;
    bool millivolts_valid;
} tcrt5000_analog_result_t;

app_status_t tcrt5000_init(const hardware_config_t *hardware,
                           const device_config_t *device);
tcrt5000_digital_result_t tcrt5000_read_digital(void);
tcrt5000_analog_result_t tcrt5000_read_analog(void);
