#pragma once

#include <stdint.h>

#include "app_status.h"
#include "device_config.h"
#include "hardware_config.h"

typedef struct {
    app_status_t status;
    float lux;
    uint16_t broadband;
    uint16_t infrared;
} tsl2561_result_t;

app_status_t tsl2561_init(const hardware_config_t *hardware,
                          const device_config_t *device);
tsl2561_result_t tsl2561_read_lux(void);
