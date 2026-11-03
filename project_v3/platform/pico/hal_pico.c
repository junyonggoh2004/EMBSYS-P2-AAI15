// platform/pico/hal_pico.c
// Pico 2 W backend for libraries/EdgeCore/src/hal/hal.h (D1 tasks 11-12).
// Each function keeps the exact SDK call sequence the shared code used before
// the HAL existed, so external behaviour is unchanged.
#include "hal/hal.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/uart.h"

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

int hal_gpio_pulse(int pin, uint32_t ms) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    // Same sequence as the reference PULSE action, which blocks for ms.
    hal_gpio_mode(pin, HAL_GPIO_OUTPUT);
    gpio_put((uint)pin, 1);
    sleep_ms(ms);
    hal_gpio_mode(pin, HAL_GPIO_OUTPUT);
    gpio_put((uint)pin, 0);
    return HAL_OK;
}

static volatile uint32_t s_edges[NUM_BANK0_GPIOS];

static void edge_isr(uint gpio, uint32_t events) {
    if (gpio < NUM_BANK0_GPIOS)
        s_edges[gpio] += ((events & GPIO_IRQ_EDGE_RISE) ? 1u : 0u) + ((events & GPIO_IRQ_EDGE_FALL) ? 1u : 0u);
}

// The reference firmware's echo measurement, moved here unchanged from the
// shared GPIO driver (D1 task 29): busy-wait for the echo to rise, then for it
// to fall, each bounded by timeout_us.
int hal_gpio_measure_pulse(int trig_pin, int echo_pin, uint32_t trig_us, uint32_t timeout_us)
{
    hal_gpio_mode(trig_pin, HAL_GPIO_OUTPUT);   // driven low
    hal_delay_us(2);

    // send trigger pulse
    hal_gpio_write(trig_pin, 1);
    hal_delay_us(trig_us);
    hal_gpio_write(trig_pin, 0);

    // wait for echo HIGH
    uint64_t t0 = hal_time_us();
    while (hal_gpio_read(echo_pin) <= 0) {
        if (hal_time_us() - t0 > timeout_us)
            return -1;
    }

    // echo HIGH -> measure width
    uint64_t start = hal_time_us();
    while (hal_gpio_read(echo_pin) > 0) {
        if (hal_time_us() - start > timeout_us)
            break;
    }
    uint64_t end = hal_time_us();
    int32_t width = (int32_t)(end - start);
    if (width < 0) width = 0;
    return (int)width; // microseconds
}

int hal_gpio_count_edges(int pin, bool enable) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    if (enable) {
        s_edges[pin] = 0;
        gpio_set_irq_enabled_with_callback((uint)pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, edge_isr);
    } else {
        gpio_set_irq_enabled((uint)pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
    }
    return HAL_OK;
}

uint32_t hal_gpio_edge_count(int pin) {
    return hal_gpio_valid(pin) ? s_edges[pin] : 0;
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

// ---------- I2C ----------
// Same controller choice and set-up as the reference bus/i2c.c.

int hal_i2c_open(int sda, int scl, uint32_t baud) {
    if (!hal_gpio_valid(sda) || !hal_gpio_valid(scl)) return HAL_ERR_PIN;
    // i2c1 groups: {2,3},{6,7},{10,11},{14,15},{18,19},{26,27}; the rest are i2c0
    int bus;
    switch (sda) {
        case 2: case 3: case 6: case 7: case 10: case 11:
        case 14: case 15: case 18: case 19: case 26: case 27:
            bus = 1; break;
        default:
            bus = 0; break;
    }
    i2c_inst_t *i2c = bus ? i2c1 : i2c0;
    i2c_init(i2c, baud);
    gpio_set_function((uint)sda, GPIO_FUNC_I2C);
    gpio_set_function((uint)scl, GPIO_FUNC_I2C);
    gpio_pull_up((uint)sda);
    gpio_pull_up((uint)scl);
    return bus;
}

int hal_i2c_write(int bus, uint8_t addr, const uint8_t *data, size_t len, bool nostop) {
    int r = i2c_write_blocking(bus ? i2c1 : i2c0, addr, data, len, nostop);
    return r < 0 ? HAL_ERR_IO : r;
}

int hal_i2c_read(int bus, uint8_t addr, uint8_t *buf, size_t len) {
    int r = i2c_read_blocking(bus ? i2c1 : i2c0, addr, buf, len, false);
    return r < 0 ? HAL_ERR_IO : r;
}

// ---------- UART ----------
// Same UART choice and set-up as the reference bus/uart.c.

static uart_inst_t *uart_for(int port) {
    return port ? uart1 : uart0;
}

int hal_uart_open(int tx, int rx, uint32_t baud, uint8_t bits, char parity, uint8_t stop) {
    if (!hal_gpio_valid(tx) || !hal_gpio_valid(rx)) return HAL_ERR_PIN;
    // uart1 pairs: {4,5},{8,9},{20,21}; the rest use uart0
    int port = 0;
    switch (tx) { case 4: case 5: case 8: case 9: case 20: case 21: port = 1; break; default: break; }
    switch (rx) { case 4: case 5: case 8: case 9: case 20: case 21: port = 1; break; default: break; }

    uart_parity_t p = UART_PARITY_NONE;
    if (parity == 'E' || parity == 'e') p = UART_PARITY_EVEN;
    if (parity == 'O' || parity == 'o') p = UART_PARITY_ODD;

    uart_inst_t *u = uart_for(port);
    uart_init(u, baud ? baud : 115200);
    uart_set_format(u, (bits >= 5 && bits <= 8) ? bits : 8, (stop == 2) ? 2 : 1, p);
    uart_set_fifo_enabled(u, true);
    gpio_set_function((uint)tx, GPIO_FUNC_UART);
    gpio_set_function((uint)rx, GPIO_FUNC_UART);
    return port;
}

int hal_uart_write(int port, const uint8_t *data, size_t len) {
    uart_write_blocking(uart_for(port), data, len);
    return (int)len;
}

int hal_uart_read_byte(int port) {
    uart_inst_t *u = uart_for(port);
    return uart_is_readable(u) ? (int)uart_getc(u) : HAL_ERR_TIMEOUT;
}

// ---------- SPI ----------
// D2 is an ESP32-S3 extension. Keep the shared EdgeCore build linkable on the
// Pico while reporting the platform limitation explicitly at runtime.

int hal_spi_open(const char *host, int sclk, int mosi, int miso, int cs,
                 uint32_t hz, uint8_t mode) {
    (void)host; (void)sclk; (void)mosi; (void)miso; (void)cs;
    (void)hz; (void)mode;
    return HAL_ERR_UNSUPPORTED;
}

int hal_spi_transfer(int device, const uint8_t *tx, uint8_t *rx, size_t len,
                     uint32_t timeout_ms) {
    (void)device; (void)tx; (void)rx; (void)len; (void)timeout_ms;
    return HAL_ERR_UNSUPPORTED;
}

int hal_spi_close(int device) {
    (void)device;
    return HAL_ERR_UNSUPPORTED;
}
