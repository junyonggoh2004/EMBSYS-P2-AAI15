#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app_status.h"
#include "device_config.h"
#include "hardware_config.h"

typedef struct {
    app_status_t status;
    size_t pixels_written;
} tft_result_t;

app_status_t tft_panel_init(const hardware_config_t *hardware,
                            const device_config_t *device);
tft_result_t tft_panel_fill(uint16_t rgb565);
tft_result_t tft_panel_draw(uint16_t x, uint16_t y, uint16_t width,
                            uint16_t height, const uint16_t *pixels);
