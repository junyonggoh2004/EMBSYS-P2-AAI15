#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"

/* Physical wiring only. Device behaviour belongs in device_config.h. */
typedef struct {
    struct {
        gpio_num_t digital_out;
        gpio_num_t analog_out;
        adc_unit_t adc_unit;
        adc_channel_t adc_channel;
    } tcrt5000;

    struct {
        i2c_port_num_t port;
        gpio_num_t sda;
        gpio_num_t scl;
        uint32_t frequency_hz;
    } i2c;

    struct {
        spi_host_device_t host;
        gpio_num_t sclk;
        gpio_num_t mosi;
        gpio_num_t miso;
        gpio_num_t cs;
        gpio_num_t dc;
        gpio_num_t reset;
        gpio_num_t backlight;
        uint32_t frequency_hz;
        uint8_t mode;
    } tft;

    struct {
        gpio_num_t trigger;
        gpio_num_t echo;
    } hcsr04;
} hardware_config_t;

extern const hardware_config_t HARDWARE_CONFIG_DEFAULT;
