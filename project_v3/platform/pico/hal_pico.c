// platform/pico/hal_pico.c
// Pico 2 W backend for include/hal/hal.h (D1 task 11).
// Each function keeps the exact SDK call sequence the shared code used before
// the HAL existed, so external behaviour is unchanged.
#include "hal/hal.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

// ---------- time ----------

uint64_t hal_time_us(void) {
    return to_us_since_boot(get_absolute_time());
}

uint32_t hal_time_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

// ---------- delay ----------

void hal_delay_ms(uint32_t ms) {
    sleep_ms(ms);
}

void hal_delay_us(uint32_t us) {
    busy_wait_us_32(us);
}

// ---------- console input ----------

int hal_console_getc(uint32_t timeout_us) {
    int ch = getchar_timeout_us(timeout_us);
    return (ch == PICO_ERROR_TIMEOUT) ? HAL_ERR_TIMEOUT : ch;
}

// ---------- GPIO ----------

bool hal_gpio_valid(int pin) {
    return pin >= 0 && pin < (int)NUM_BANK0_GPIOS;
}

int hal_gpio_mode(int pin, hal_gpio_mode_t mode) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    gpio_init((uint)pin);
    if (mode == HAL_GPIO_OUTPUT) {
        gpio_set_dir((uint)pin, GPIO_OUT);
        return HAL_OK;
    }
    gpio_set_dir((uint)pin, GPIO_IN);
    if (mode == HAL_GPIO_INPUT_PULLUP)        gpio_pull_up((uint)pin);
    else if (mode == HAL_GPIO_INPUT_PULLDOWN) gpio_pull_down((uint)pin);
    else                                      gpio_disable_pulls((uint)pin);
    return HAL_OK;
}

int hal_gpio_write(int pin, int level) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    gpio_put((uint)pin, level ? 1 : 0);
    return HAL_OK;
}

int hal_gpio_read(int pin) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    return gpio_get((uint)pin) ? 1 : 0;
}

int hal_gpio_toggle(int pin) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    // Same sequence as the reference TOGGLE action. gpio_init() clears the
    // output latch, so the level comes from the input read straight after
    // the switch to output; keep these calls together.
    gpio_init((uint)pin);
    gpio_set_dir((uint)pin, GPIO_OUT);
    int next = !gpio_get((uint)pin);
    gpio_put((uint)pin, next);
    return next;
}

// ---------- PWM ----------

int hal_pwm_set(int pin, float duty) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    gpio_set_function((uint)pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num((uint)pin);
    uint chan  = pwm_gpio_to_channel((uint)pin);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 4.0f);
    pwm_config_set_wrap(&cfg, 1000);        // 0..1000 => ~10-bit resolution
    pwm_init(slice, &cfg, false);

    uint16_t level = (uint16_t)(duty * 1000.0f);
    pwm_set_chan_level(slice, chan, level);
    pwm_set_enabled(slice, true);
    return HAL_OK;
}

int hal_pwm_stop(int pin) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    gpio_set_function((uint)pin, GPIO_FUNC_SIO);
    gpio_init((uint)pin);
    gpio_set_dir((uint)pin, GPIO_OUT);
    gpio_put((uint)pin, 0);
    return HAL_OK;
}
