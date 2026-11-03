#include "bus_registry.h"
#include "device_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardware_config.h"
#include "hcsr04.h"
#include "tcrt5000.h"
#include "tft_panel.h"
#include "tsl2561.h"

static const char *TAG = "sensor_hub";

const hardware_config_t HARDWARE_CONFIG_DEFAULT = {
    .tcrt5000 = {
        .digital_out = GPIO_NUM_4,
        .analog_out = GPIO_NUM_1,
        .adc_unit = ADC_UNIT_1,
        .adc_channel = ADC_CHANNEL_0,
    },
    .i2c = {
        .port = I2C_NUM_0,
        .sda = GPIO_NUM_8,
        .scl = GPIO_NUM_9,
        .frequency_hz = 100000,
    },
    .tft = {
        .host = SPI2_HOST,
        .sclk = GPIO_NUM_12,
        .mosi = GPIO_NUM_11,
        .miso = GPIO_NUM_NC,
        .cs = GPIO_NUM_10,
        .dc = GPIO_NUM_7,
        .reset = GPIO_NUM_6,
        .backlight = GPIO_NUM_5,
        .frequency_hz = 10 * 1000 * 1000,
        .mode = 0,
    },
    .hcsr04 = {
        .trigger = GPIO_NUM_2,
        .echo = GPIO_NUM_13,
    },
};

const device_config_t DEVICE_CONFIG_DEFAULT = {
    .tcrt5000 = {
        .digital_active_low = true,
    },
    .tsl2561 = {
        .address = 0x29,
        .gain = TSL2561_GAIN_1X,
        .integration = TSL2561_INTEGRATION_402_MS,
    },
    .tft = {
        .controller = TFT_CONTROLLER_ST7789P3,
        .width = 76,
        .height = 284,
        .x_offset = 82,
        .y_offset = 18,
        .rotation = TFT_ROTATION_0,
        .bgr_order = false,
        .inverted = true,
        /* GPIO5 is not claimed or driven until this is explicitly true. */
        .backlight_is_logic_input = false,
        .backlight_active_high = true,
    },
    .hcsr04 = {
        .enabled = false,
        .echo_level_shifter_installed = false,
        .timeout_us = 30000,
    },
};

static app_status_t add(bus_registry_t *registry, bus_resource_t resource)
{
    const app_status_t status = bus_registry_register(registry, &resource);
    if (status != APP_STATUS_OK) {
        ESP_LOGE(TAG, "could not register %s: %s", resource.owner,
                 app_status_name(status));
    }
    return status;
}

static app_status_t register_hardware(bus_registry_t *registry,
                                      const hardware_config_t *hardware,
                                      const device_config_t *device)
{
    bus_registry_reset(registry);
    if (add(registry, (bus_resource_t){
            .type = BUS_RESOURCE_GPIO, .owner = "tcrt5000.do",
            .config.gpio = {hardware->tcrt5000.digital_out, "DO"}}) != APP_STATUS_OK ||
        add(registry, (bus_resource_t){
            .type = BUS_RESOURCE_ADC, .owner = "tcrt5000.ao",
            .config.adc = {hardware->tcrt5000.analog_out,
                           hardware->tcrt5000.adc_unit,
                           hardware->tcrt5000.adc_channel}}) != APP_STATUS_OK ||
        add(registry, (bus_resource_t){
            .type = BUS_RESOURCE_I2C_BUS, .owner = "i2c0",
            .config.i2c_bus = {hardware->i2c.port, hardware->i2c.sda,
                               hardware->i2c.scl, hardware->i2c.frequency_hz}}) != APP_STATUS_OK ||
        add(registry, (bus_resource_t){
            .type = BUS_RESOURCE_I2C_DEVICE, .owner = "tsl2561",
            .config.i2c_device = {hardware->i2c.port,
                                  device->tsl2561.address}}) != APP_STATUS_OK ||
        add(registry, (bus_resource_t){
            .type = BUS_RESOURCE_SPI_BUS, .owner = "spi2",
            .config.spi_bus = {hardware->tft.host, hardware->tft.sclk,
                               hardware->tft.mosi, hardware->tft.miso}}) != APP_STATUS_OK ||
        add(registry, (bus_resource_t){
            .type = BUS_RESOURCE_SPI_DEVICE, .owner = "tft.cs",
            .config.spi_device = {hardware->tft.host,
                                  hardware->tft.cs}}) != APP_STATUS_OK ||
        add(registry, (bus_resource_t){
            .type = BUS_RESOURCE_GPIO, .owner = "tft.dc",
            .config.gpio = {hardware->tft.dc, "DC"}}) != APP_STATUS_OK ||
        add(registry, (bus_resource_t){
            .type = BUS_RESOURCE_GPIO, .owner = "tft.reset",
            .config.gpio = {hardware->tft.reset, "RST"}}) != APP_STATUS_OK) {
        return APP_STATUS_NO_MEMORY;
    }

    if (device->tft.backlight_is_logic_input &&
        add(registry, (bus_resource_t){
            .type = BUS_RESOURCE_GPIO, .owner = "tft.backlight",
            .config.gpio = {hardware->tft.backlight, "BL"}}) != APP_STATUS_OK) {
        return APP_STATUS_NO_MEMORY;
    }

    /* GPIO2/GPIO13 remain unconfigured until both safety flags are true. */
    if (device->hcsr04.enabled) {
        if (!device->hcsr04.echo_level_shifter_installed) {
            ESP_LOGE(TAG, "HC-SR04 enable rejected: Echo level shifter not confirmed");
            return APP_STATUS_DISABLED;
        }
        if (add(registry, (bus_resource_t){
                .type = BUS_RESOURCE_GPIO, .owner = "hcsr04.trigger",
                .config.gpio = {hardware->hcsr04.trigger, "TRIG"}}) != APP_STATUS_OK ||
            add(registry, (bus_resource_t){
                .type = BUS_RESOURCE_GPIO, .owner = "hcsr04.echo",
                .config.gpio = {hardware->hcsr04.echo, "ECHO_DIVIDED"}}) != APP_STATUS_OK) {
            return APP_STATUS_NO_MEMORY;
        }
    }

    const char *owner_a;
    const char *owner_b;
    gpio_num_t pin;
    const app_status_t status = bus_registry_validate(registry, &owner_a,
                                                       &owner_b, &pin);
    if (status != APP_STATUS_OK) {
        ESP_LOGE(TAG, "pin validation failed: %s, GPIO%d, %s / %s",
                 app_status_name(status), pin, owner_a != NULL ? owner_a : "-",
                 owner_b != NULL ? owner_b : "-");
    }
    return status;
}

void app_main(void)
{
    const hardware_config_t *hardware = &HARDWARE_CONFIG_DEFAULT;
    const device_config_t *device = &DEVICE_CONFIG_DEFAULT;
    bus_registry_t registry;

    app_status_t status = register_hardware(&registry, hardware, device);
    if (status != APP_STATUS_OK || !bus_registry_is_validated(&registry)) {
        ESP_LOGE(TAG, "hardware initialization blocked by registry: %s",
                 app_status_name(status));
        return;
    }

    status = tcrt5000_init(hardware, device);
    ESP_LOGI(TAG, "TCRT5000 init: %s", app_status_name(status));
    const app_status_t light_status = tsl2561_init(hardware, device);
    ESP_LOGI(TAG, "TSL2561 init: %s", app_status_name(light_status));
    const app_status_t display_status = tft_panel_init(hardware, device);
    ESP_LOGI(TAG, "TFT init: %s", app_status_name(display_status));
    const app_status_t range_status = hcsr04_init(hardware, device);
    ESP_LOGI(TAG, "HC-SR04 init: %s", app_status_name(range_status));

    if (display_status == APP_STATUS_OK) {
        const tft_result_t fill = tft_panel_fill(0x0000);
        ESP_LOGI(TAG, "TFT clear: %s (%u pixels)", app_status_name(fill.status),
                 (unsigned)fill.pixels_written);
    }

    while (true) {
        const tcrt5000_digital_result_t digital = tcrt5000_read_digital();
        if (digital.status == APP_STATUS_OK) {
            ESP_LOGI(TAG, "TCRT DO: raw=%d detected=%s", digital.raw_level,
                     digital.detected ? "true" : "false");
        } else {
            ESP_LOGW(TAG, "TCRT DO unavailable: %s",
                     app_status_name(digital.status));
        }

        const tcrt5000_analog_result_t analog = tcrt5000_read_analog();
        if (analog.status == APP_STATUS_OK) {
            if (analog.millivolts_valid) {
                ESP_LOGI(TAG, "TCRT AO: raw=%d, %d mV", analog.raw,
                         analog.millivolts);
            } else {
                ESP_LOGI(TAG, "TCRT AO: raw=%d (uncalibrated)", analog.raw);
            }
        } else {
            ESP_LOGW(TAG, "TCRT AO unavailable: %s",
                     app_status_name(analog.status));
        }

        const tsl2561_result_t light = tsl2561_read_lux();
        if (light.status == APP_STATUS_OK) {
            ESP_LOGI(TAG, "TSL2561: %.2f lux (broadband=%u IR=%u)",
                     (double)light.lux, light.broadband, light.infrared);
        } else {
            ESP_LOGW(TAG, "TSL2561 reading unavailable: %s (broadband=%u IR=%u)",
                     app_status_name(light.status), light.broadband,
                     light.infrared);
        }

        if (device->hcsr04.enabled) {
            const hcsr04_result_t range = hcsr04_read();
            if (range.status == APP_STATUS_OK) {
                ESP_LOGI(TAG, "HC-SR04: %.1f cm", (double)range.distance_cm);
            } else {
                ESP_LOGW(TAG, "HC-SR04 reading unavailable: %s",
                         app_status_name(range.status));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
