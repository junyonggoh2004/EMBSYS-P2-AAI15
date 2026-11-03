#pragma once

#include "app_status.h"
#include "device_config.h"
#include "hardware_config.h"

typedef struct {
    app_status_t status;
    float distance_cm;
} hcsr04_result_t;

app_status_t hcsr04_init(const hardware_config_t *hardware,
                         const device_config_t *device);
hcsr04_result_t hcsr04_read(void);
