#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

/* Grove - Digital Light Sensor v1.1: TSL2561 at I2C address 0x29. */
#define TSL2561_ADDR          0x29
#define TSL2561_COMMAND       0x80
#define TSL2561_REG_CONTROL   0x00
#define TSL2561_REG_TIMING    0x01
#define TSL2561_REG_ID        0x0A
#define TSL2561_REG_CH0_LOW   0x0C

#define I2C_PORT              i2c0
#define I2C_SDA_GPIO          16
#define I2C_SCL_GPIO          17

static bool tsl2561_write(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = { TSL2561_COMMAND | reg, value };
    return i2c_write_blocking(I2C_PORT, TSL2561_ADDR, data, sizeof(data), false) == (int)sizeof(data);
}

static bool tsl2561_read(uint8_t reg, uint8_t *data, size_t length)
{
    const uint8_t command = TSL2561_COMMAND | reg;
    if (i2c_write_blocking(I2C_PORT, TSL2561_ADDR, &command, 1, true) != 1) {
        return false;
    }
    return i2c_read_blocking(I2C_PORT, TSL2561_ADDR, data, length, false) == (int)length;
}

static float calculate_lux(uint16_t full_spectrum, uint16_t infrared)
{
    if (full_spectrum == 0) return 0.0f;

    /* The sensor is configured for 402 ms / 1x gain. The TSL2561 equations
       use readings normalized to 402 ms / 16x gain. */
    const float ch0 = (float)full_spectrum * 16.0f;
    const float ch1 = (float)infrared * 16.0f;
    const float ratio = ch1 / ch0;

    if (ratio <= 0.50f) return 0.0304f * ch0 - 0.062f * ch0 * powf(ratio, 1.4f);
    if (ratio <= 0.61f) return 0.0224f * ch0 - 0.031f * ch1;
    if (ratio <= 0.80f) return 0.0128f * ch0 - 0.0153f * ch1;
    if (ratio <= 1.30f) return 0.00146f * ch0 - 0.00112f * ch1;
    return 0.0f;
}

int main(void)
{
    stdio_init_all();

    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(I2C_SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_GPIO);
    gpio_pull_up(I2C_SCL_GPIO);

    sleep_ms(1500);
    printf("\r\nGrove Digital Light Sensor v1.1 (TSL2561)\r\n");
    printf("I2C0: SDA=GP16 (white), SCL=GP17 (yellow), address=0x29\r\n");

    uint8_t id = 0;
    if (!tsl2561_read(TSL2561_REG_ID, &id, 1)) {
        printf("ERROR: TSL2561 not found. Check red=3V3, black=GND, white=GP16, yellow=GP17.\r\n");
        while (true) sleep_ms(1000);
    }
    printf("TSL2561 detected (ID=0x%02X)\r\n", id);

    if (!tsl2561_write(TSL2561_REG_CONTROL, 0x03) ||  /* power on */
        !tsl2561_write(TSL2561_REG_TIMING, 0x02)) {   /* 402 ms, 1x gain */
        printf("ERROR: could not configure TSL2561.\r\n");
        while (true) sleep_ms(1000);
    }

    sleep_ms(450); /* wait for the first 402 ms measurement */
    while (true) {
        uint8_t data[4];
        if (!tsl2561_read(TSL2561_REG_CH0_LOW, data, sizeof(data))) {
            printf("ERROR: sensor read failed\r\n");
        } else {
            const uint16_t full_spectrum = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
            const uint16_t infrared = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
            float lux = calculate_lux(full_spectrum, infrared);
            if (lux < 0.0f) lux = 0.0f;
            printf("light: %.1f lux  (full=%u IR=%u)\r\n",
                   (double)lux, full_spectrum, infrared);
        }
        sleep_ms(1000);
    }
}
