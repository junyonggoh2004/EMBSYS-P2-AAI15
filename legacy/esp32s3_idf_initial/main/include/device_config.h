#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TSL2561_GAIN_1X = 0,
    TSL2561_GAIN_16X = 1,
} tsl2561_gain_t;

typedef enum {
    TSL2561_INTEGRATION_13_MS = 0,
    TSL2561_INTEGRATION_101_MS = 1,
    TSL2561_INTEGRATION_402_MS = 2,
} tsl2561_integration_t;

typedef enum {
    TFT_CONTROLLER_ST7789P3 = 0,
    TFT_CONTROLLER_NV3023B = 1,
} tft_controller_t;

typedef enum {
    TFT_ROTATION_0 = 0,
    TFT_ROTATION_90 = 1,
    TFT_ROTATION_180 = 2,
    TFT_ROTATION_270 = 3,
} tft_rotation_t;

typedef struct {
    struct {
        bool digital_active_low;
    } tcrt5000;

    struct {
        uint8_t address;
        tsl2561_gain_t gain;
        tsl2561_integration_t integration;
    } tsl2561;

    struct {
        tft_controller_t controller;
        uint16_t width;
        uint16_t height;
        uint16_t x_offset;
        uint16_t y_offset;
        tft_rotation_t rotation;
        bool bgr_order;
        bool inverted;
        bool backlight_is_logic_input;
        bool backlight_active_high;
    } tft;

    struct {
        bool enabled;
        bool echo_level_shifter_installed;
        uint32_t timeout_us;
    } hcsr04;
} device_config_t;

extern const device_config_t DEVICE_CONFIG_DEFAULT;
