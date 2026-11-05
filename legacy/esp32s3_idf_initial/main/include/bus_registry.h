#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_status.h"
#include "driver/gpio.h"

#define BUS_REGISTRY_MAX_RESOURCES 24

typedef enum {
    BUS_RESOURCE_GPIO = 0,
    BUS_RESOURCE_ADC,
    BUS_RESOURCE_I2C_BUS,
    BUS_RESOURCE_I2C_DEVICE,
    BUS_RESOURCE_SPI_BUS,
    BUS_RESOURCE_SPI_DEVICE,
} bus_resource_type_t;

typedef struct {
    bus_resource_type_t type;
    const char *owner;
    union {
        struct { gpio_num_t pin; const char *signal; } gpio;
        struct { gpio_num_t pin; int unit; int channel; } adc;
        struct { int port; gpio_num_t sda; gpio_num_t scl; uint32_t frequency_hz; } i2c_bus;
        struct { int port; uint8_t address; } i2c_device;
        struct { int host; gpio_num_t sclk; gpio_num_t mosi; gpio_num_t miso; } spi_bus;
        struct { int host; gpio_num_t cs; } spi_device;
    } config;
} bus_resource_t;

typedef struct {
    bus_resource_t resources[BUS_REGISTRY_MAX_RESOURCES];
    size_t count;
    bool validated;
} bus_registry_t;

void bus_registry_reset(bus_registry_t *registry);
app_status_t bus_registry_register(bus_registry_t *registry,
                                   const bus_resource_t *resource);
app_status_t bus_registry_validate(bus_registry_t *registry,
                                   const char **owner_a,
                                   const char **owner_b,
                                   gpio_num_t *conflicting_pin);
bool bus_registry_is_validated(const bus_registry_t *registry);
