#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

/* Grove Digital Light Sensor v1.1 (TSL2561) on I2C0. */
#define TSL2561_ADDR          0x29
#define TSL2561_COMMAND       0x80
#define TSL2561_REG_CONTROL   0x00
#define TSL2561_REG_TIMING    0x01
#define TSL2561_REG_ID        0x0A
#define TSL2561_REG_CH0_LOW   0x0C
#define LIGHT_I2C             i2c0
#define LIGHT_SDA_GPIO        16
#define LIGHT_SCL_GPIO        17

/* HC-SR04: Echo must pass through a 5 V -> 3.3 V level divider. */
#define HCSR04_TRIG_GPIO      18
#define HCSR04_ECHO_GPIO      19

/* TCRT5000 reflective line-tracking module's digital output (DO). */
#define TCRT5000_DO_GPIO      20
#define TCRT5000_ACTIVE_LEVEL 0

static bool tsl2561_write(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = { TSL2561_COMMAND | reg, value };
    return i2c_write_blocking(LIGHT_I2C, TSL2561_ADDR, data, sizeof(data), false) == (int)sizeof(data);
}

static bool tsl2561_read(uint8_t reg, uint8_t *data, size_t length)
{
    const uint8_t command = TSL2561_COMMAND | reg;
    return i2c_write_blocking(LIGHT_I2C, TSL2561_ADDR, &command, 1, true) == 1 &&
           i2c_read_blocking(LIGHT_I2C, TSL2561_ADDR, data, length, false) == (int)length;
}

static float tsl2561_lux(uint16_t full_spectrum, uint16_t infrared)
{
    if (full_spectrum == 0) return 0.0f;
    /* 402 ms / 1x readings normalized to the TSL2561's 402 ms / 16x formula. */
    const float ch0 = (float)full_spectrum * 16.0f;
    const float ch1 = (float)infrared * 16.0f;
    const float ratio = ch1 / ch0;

    if (ratio <= 0.50f) return 0.0304f * ch0 - 0.062f * ch0 * powf(ratio, 1.4f);
    if (ratio <= 0.61f) return 0.0224f * ch0 - 0.031f * ch1;
    if (ratio <= 0.80f) return 0.0128f * ch0 - 0.0153f * ch1;
    if (ratio <= 1.30f) return 0.00146f * ch0 - 0.00112f * ch1;
    return 0.0f;
}

static bool hcsr04_distance_cm(float *distance_cm)
{
    const uint32_t timeout_us = 30000;

    gpio_put(HCSR04_TRIG_GPIO, 0);
    sleep_us(2);
    gpio_put(HCSR04_TRIG_GPIO, 1);
    sleep_us(10);
    gpio_put(HCSR04_TRIG_GPIO, 0);

    const uint32_t wait_start = time_us_32();
    while (!gpio_get(HCSR04_ECHO_GPIO)) {
        if ((uint32_t)(time_us_32() - wait_start) > timeout_us) return false;
    }

    const uint32_t pulse_start = time_us_32();
    while (gpio_get(HCSR04_ECHO_GPIO)) {
        if ((uint32_t)(time_us_32() - pulse_start) > timeout_us) return false;
    }

    *distance_cm = (float)(time_us_32() - pulse_start) * 0.01715f;
    return true;
}

int main(void)
{
    stdio_init_all();

    i2c_init(LIGHT_I2C, 100 * 1000);
    gpio_set_function(LIGHT_SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(LIGHT_SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(LIGHT_SDA_GPIO);
    gpio_pull_up(LIGHT_SCL_GPIO);

    gpio_init(HCSR04_TRIG_GPIO);
    gpio_set_dir(HCSR04_TRIG_GPIO, GPIO_OUT);
    gpio_put(HCSR04_TRIG_GPIO, 0);
    gpio_init(HCSR04_ECHO_GPIO);
    gpio_set_dir(HCSR04_ECHO_GPIO, GPIO_IN);

    gpio_init(TCRT5000_DO_GPIO);
    gpio_set_dir(TCRT5000_DO_GPIO, GPIO_IN);
    gpio_pull_up(TCRT5000_DO_GPIO);

    sleep_ms(1500);
    printf("\r\nPico 2 W combined sensor monitor\r\n");
    printf("Light: TSL2561 I2C0 GP16/GP17; distance: HC-SR04 GP18/GP19; line sensor DO: GP20\r\n");

    uint8_t id = 0;
    const bool light_present = tsl2561_read(TSL2561_REG_ID, &id, 1) &&
                               tsl2561_write(TSL2561_REG_CONTROL, 0x03) &&
                               tsl2561_write(TSL2561_REG_TIMING, 0x02);
    printf(light_present ? "TSL2561 ready (ID=0x%02X)\r\n" : "TSL2561 NOT FOUND - check Grove wiring\r\n", id);
    if (light_present) sleep_ms(450);

    while (true) {
        if (light_present) {
            uint8_t channels[4];
            if (tsl2561_read(TSL2561_REG_CH0_LOW, channels, sizeof(channels))) {
                const uint16_t full = (uint16_t)channels[0] | ((uint16_t)channels[1] << 8);
                const uint16_t infrared = (uint16_t)channels[2] | ((uint16_t)channels[3] << 8);
                float lux = tsl2561_lux(full, infrared);
                printf("light: %.1f lux  ", (double)(lux < 0.0f ? 0.0f : lux));
            } else {
                printf("light: read error  ");
            }
        }

        float distance_cm;
        if (hcsr04_distance_cm(&distance_cm)) {
            printf("distance: %.1f cm  ", (double)distance_cm);
        } else {
            printf("distance: out of range/no echo  ");
        }

        const bool line_detected = gpio_get(TCRT5000_DO_GPIO) == TCRT5000_ACTIVE_LEVEL;
        printf("line: %s\r\n", line_detected ? "DETECTED" : "not detected");
        sleep_ms(1000);
    }
}
