#include "hcsr04.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

typedef struct {
    bool initialized;
    gpio_num_t trigger;
    gpio_num_t echo;
    uint32_t timeout_us;
} hcsr04_context_t;

static hcsr04_context_t s_hcsr;

app_status_t hcsr04_init(const hardware_config_t *hardware,
                         const device_config_t *device)
{
    if (hardware == NULL || device == NULL) return APP_STATUS_INVALID_ARGUMENT;
    memset(&s_hcsr, 0, sizeof(s_hcsr));
    if (!device->hcsr04.enabled) return APP_STATUS_DISABLED;
    if (!device->hcsr04.echo_level_shifter_installed) {
        return APP_STATUS_DISABLED;
    }

    gpio_config_t trigger = {
        .pin_bit_mask = 1ULL << hardware->hcsr04.trigger,
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t echo = {
        .pin_bit_mask = 1ULL << hardware->hcsr04.echo,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&trigger) != ESP_OK || gpio_config(&echo) != ESP_OK) {
        return APP_STATUS_INVALID_ARGUMENT;
    }
    gpio_set_level(hardware->hcsr04.trigger, 0);
    s_hcsr.trigger = hardware->hcsr04.trigger;
    s_hcsr.echo = hardware->hcsr04.echo;
    s_hcsr.timeout_us = device->hcsr04.timeout_us != 0
                            ? device->hcsr04.timeout_us : 30000;
    s_hcsr.initialized = true;
    return APP_STATUS_OK;
}

static bool wait_for_level(int level, uint32_t timeout_us, int64_t *timestamp)
{
    const int64_t start = esp_timer_get_time();
    while (gpio_get_level(s_hcsr.echo) != level) {
        if ((uint64_t)(esp_timer_get_time() - start) >= timeout_us) return false;
    }
    if (timestamp != NULL) *timestamp = esp_timer_get_time();
    return true;
}

hcsr04_result_t hcsr04_read(void)
{
    hcsr04_result_t result = {.status = APP_STATUS_DISABLED};
    if (!s_hcsr.initialized) return result;

    gpio_set_level(s_hcsr.trigger, 0);
    esp_rom_delay_us(2);
    gpio_set_level(s_hcsr.trigger, 1);
    esp_rom_delay_us(10);
    gpio_set_level(s_hcsr.trigger, 0);

    int64_t pulse_start;
    if (!wait_for_level(1, s_hcsr.timeout_us, &pulse_start)) {
        result.status = APP_STATUS_TIMEOUT;
        return result;
    }
    int64_t pulse_end;
    if (!wait_for_level(0, s_hcsr.timeout_us, &pulse_end)) {
        result.status = APP_STATUS_TIMEOUT;
        return result;
    }
    result.distance_cm = (float)(pulse_end - pulse_start) * 0.01715f;
    result.status = APP_STATUS_OK;
    return result;
}
