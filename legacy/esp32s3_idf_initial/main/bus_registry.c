#include "bus_registry.h"

#include <string.h>

/* GPIO0 boot, GPIO19/20 native USB, GPIO48 onboard RGB LED. */
static bool is_board_reserved(gpio_num_t pin)
{
    return pin == GPIO_NUM_0 || pin == GPIO_NUM_19 ||
           pin == GPIO_NUM_20 || pin == GPIO_NUM_48;
}

static size_t resource_pins(const bus_resource_t *resource, gpio_num_t pins[3])
{
    switch (resource->type) {
    case BUS_RESOURCE_GPIO:
        pins[0] = resource->config.gpio.pin;
        return 1;
    case BUS_RESOURCE_ADC:
        pins[0] = resource->config.adc.pin;
        return 1;
    case BUS_RESOURCE_I2C_BUS:
        pins[0] = resource->config.i2c_bus.sda;
        pins[1] = resource->config.i2c_bus.scl;
        return 2;
    case BUS_RESOURCE_SPI_BUS:
        pins[0] = resource->config.spi_bus.sclk;
        pins[1] = resource->config.spi_bus.mosi;
        pins[2] = resource->config.spi_bus.miso;
        return resource->config.spi_bus.miso == GPIO_NUM_NC ? 2 : 3;
    case BUS_RESOURCE_SPI_DEVICE:
        pins[0] = resource->config.spi_device.cs;
        return resource->config.spi_device.cs == GPIO_NUM_NC ? 0 : 1;
    case BUS_RESOURCE_I2C_DEVICE:
    default:
        return 0;
    }
}

const char *app_status_name(app_status_t status)
{
    static const char *const names[] = {
        "ok", "invalid_argument", "not_initialized", "no_memory",
        "gpio_conflict", "reserved_gpio", "gpio_error", "i2c_error",
        "spi_error", "adc_error", "timeout", "saturated", "disabled",
        "stale"
    };
    return ((unsigned)status < (sizeof(names) / sizeof(names[0])))
               ? names[status]
               : "unknown";
}

void bus_registry_reset(bus_registry_t *registry)
{
    if (registry != NULL) {
        memset(registry, 0, sizeof(*registry));
    }
}

app_status_t bus_registry_register(bus_registry_t *registry,
                                   const bus_resource_t *resource)
{
    if (registry == NULL || resource == NULL || resource->owner == NULL) {
        return APP_STATUS_INVALID_ARGUMENT;
    }
    if (registry->count >= BUS_REGISTRY_MAX_RESOURCES) {
        return APP_STATUS_NO_MEMORY;
    }
    registry->resources[registry->count++] = *resource;
    registry->validated = false;
    return APP_STATUS_OK;
}

app_status_t bus_registry_validate(bus_registry_t *registry,
                                   const char **owner_a,
                                   const char **owner_b,
                                   gpio_num_t *conflicting_pin)
{
    if (registry == NULL) return APP_STATUS_INVALID_ARGUMENT;

    if (owner_a != NULL) *owner_a = NULL;
    if (owner_b != NULL) *owner_b = NULL;
    if (conflicting_pin != NULL) *conflicting_pin = GPIO_NUM_NC;

    for (size_t i = 0; i < registry->count; ++i) {
        gpio_num_t left[3];
        const size_t left_count = resource_pins(&registry->resources[i], left);
        for (size_t p = 0; p < left_count; ++p) {
            if (!GPIO_IS_VALID_GPIO(left[p]) || is_board_reserved(left[p])) {
                if (owner_a != NULL) *owner_a = registry->resources[i].owner;
                if (conflicting_pin != NULL) *conflicting_pin = left[p];
                return is_board_reserved(left[p]) ? APP_STATUS_RESERVED_GPIO
                                                  : APP_STATUS_INVALID_ARGUMENT;
            }
            for (size_t q = p + 1; q < left_count; ++q) {
                if (left[p] == left[q]) {
                    if (owner_a != NULL) *owner_a = registry->resources[i].owner;
                    if (owner_b != NULL) *owner_b = registry->resources[i].owner;
                    if (conflicting_pin != NULL) *conflicting_pin = left[p];
                    return APP_STATUS_GPIO_CONFLICT;
                }
            }
            for (size_t j = i + 1; j < registry->count; ++j) {
                gpio_num_t right[3];
                const size_t right_count = resource_pins(&registry->resources[j], right);
                for (size_t q = 0; q < right_count; ++q) {
                    if (left[p] == right[q]) {
                        if (owner_a != NULL) *owner_a = registry->resources[i].owner;
                        if (owner_b != NULL) *owner_b = registry->resources[j].owner;
                        if (conflicting_pin != NULL) *conflicting_pin = left[p];
                        return APP_STATUS_GPIO_CONFLICT;
                    }
                }
            }
        }
    }
    registry->validated = true;
    return APP_STATUS_OK;
}

bool bus_registry_is_validated(const bus_registry_t *registry)
{
    return registry != NULL && registry->validated;
}
