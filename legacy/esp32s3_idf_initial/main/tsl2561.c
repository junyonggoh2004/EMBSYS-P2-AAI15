#include "tsl2561.h"

#include <math.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TSL_COMMAND       0x80
#define TSL_WORD          0x20
#define TSL_BLOCK         0x10
#define TSL_REG_CONTROL   0x00
#define TSL_REG_TIMING    0x01
#define TSL_REG_ID        0x0A
#define TSL_REG_DATA0LOW  0x0C
#define TSL_POWER_ON      0x03

typedef struct {
    bool initialized;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t device;
    tsl2561_gain_t gain;
    tsl2561_integration_t integration;
} tsl2561_context_t;

static tsl2561_context_t s_tsl;

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = {TSL_COMMAND | reg, value};
    return i2c_master_transmit(s_tsl.device, bytes, sizeof(bytes), 100);
}

static esp_err_t read_registers(uint8_t reg, uint8_t *data, size_t length)
{
    const uint8_t protocol = length == 1 ? 0 : (length == 2 ? TSL_WORD : TSL_BLOCK);
    const uint8_t command = TSL_COMMAND | protocol | reg;
    return i2c_master_transmit_receive(s_tsl.device, &command, 1,
                                       data, length, 100);
}

static uint32_t integration_delay_ms(tsl2561_integration_t integration)
{
    static const uint16_t delays[] = {15, 105, 405};
    return delays[integration];
}

static uint16_t saturation_limit(tsl2561_integration_t integration)
{
    static const uint16_t limits[] = {4900, 37000, 65000};
    return limits[integration];
}

app_status_t tsl2561_init(const hardware_config_t *hardware,
                          const device_config_t *device)
{
    if (hardware == NULL || device == NULL ||
        device->tsl2561.address > 0x7f ||
        device->tsl2561.integration > TSL2561_INTEGRATION_402_MS ||
        device->tsl2561.gain > TSL2561_GAIN_16X) {
        return APP_STATUS_INVALID_ARGUMENT;
    }
    memset(&s_tsl, 0, sizeof(s_tsl));

    i2c_master_bus_config_t bus_config = {
        .i2c_port = hardware->i2c.port,
        .sda_io_num = hardware->i2c.sda,
        .scl_io_num = hardware->i2c.scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_config, &s_tsl.bus) != ESP_OK) {
        return APP_STATUS_I2C_ERROR;
    }

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device->tsl2561.address,
        .scl_speed_hz = hardware->i2c.frequency_hz,
    };
    if (i2c_master_bus_add_device(s_tsl.bus, &device_config,
                                  &s_tsl.device) != ESP_OK) {
        i2c_del_master_bus(s_tsl.bus);
        memset(&s_tsl, 0, sizeof(s_tsl));
        return APP_STATUS_I2C_ERROR;
    }

    uint8_t id = 0;
    if (read_registers(TSL_REG_ID, &id, 1) != ESP_OK ||
        write_register(TSL_REG_CONTROL, TSL_POWER_ON) != ESP_OK) {
        return APP_STATUS_I2C_ERROR;
    }
    const uint8_t timing = (uint8_t)device->tsl2561.integration |
                           (device->tsl2561.gain == TSL2561_GAIN_16X ? 0x10 : 0x00);
    if (write_register(TSL_REG_TIMING, timing) != ESP_OK) {
        return APP_STATUS_I2C_ERROR;
    }

    s_tsl.gain = device->tsl2561.gain;
    s_tsl.integration = device->tsl2561.integration;
    s_tsl.initialized = true;
    vTaskDelay(pdMS_TO_TICKS(integration_delay_ms(s_tsl.integration)));
    return APP_STATUS_OK;
}

static float calculate_lux(uint16_t broadband, uint16_t infrared)
{
    if (broadband == 0) return 0.0f;

    static const float integration_scale[] = {
        402.0f / 13.7f, 402.0f / 101.0f, 1.0f
    };
    float scale = integration_scale[s_tsl.integration];
    if (s_tsl.gain == TSL2561_GAIN_1X) scale *= 16.0f;

    const float channel0 = broadband * scale;
    const float channel1 = infrared * scale;
    const float ratio = channel1 / channel0;
    float lux;
    if (ratio <= 0.50f) {
        lux = 0.0304f * channel0 - 0.062f * channel0 * powf(ratio, 1.4f);
    } else if (ratio <= 0.61f) {
        lux = 0.0224f * channel0 - 0.031f * channel1;
    } else if (ratio <= 0.80f) {
        lux = 0.0128f * channel0 - 0.0153f * channel1;
    } else if (ratio <= 1.30f) {
        lux = 0.00146f * channel0 - 0.00112f * channel1;
    } else {
        lux = 0.0f;
    }
    return fmaxf(lux, 0.0f);
}

tsl2561_result_t tsl2561_read_lux(void)
{
    tsl2561_result_t result = {.status = APP_STATUS_NOT_INITIALIZED};
    if (!s_tsl.initialized) return result;

    uint8_t bytes[4];
    if (read_registers(TSL_REG_DATA0LOW, bytes, sizeof(bytes)) != ESP_OK) {
        result.status = APP_STATUS_I2C_ERROR;
        return result;
    }
    result.broadband = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    result.infrared = (uint16_t)bytes[2] | ((uint16_t)bytes[3] << 8);
    const uint16_t limit = saturation_limit(s_tsl.integration);
    if (result.broadband >= limit || result.infrared >= limit) {
        result.status = APP_STATUS_SATURATED;
        return result;
    }
    result.lux = calculate_lux(result.broadband, result.infrared);
    result.status = APP_STATUS_OK;
    return result;
}
