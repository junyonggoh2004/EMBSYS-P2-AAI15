#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bus/bus_common.h"
#include "bus/bus_if.h"
#include "hal/hal.h"

static unsigned mode_calls;

bool hal_gpio_valid(int pin) { return pin >= 0 && pin < 49; }
int hal_gpio_mode(int pin, hal_gpio_mode_t mode)
{ (void)pin; (void)mode; ++mode_calls; return HAL_OK; }
int hal_gpio_read(int pin) { (void)pin; return 0; }
int hal_gpio_write(int pin, int value) { (void)pin; (void)value; return HAL_OK; }
int hal_gpio_count_edges(int pin, bool enable)
{ (void)pin; (void)enable; return HAL_OK; }
uint32_t hal_gpio_edge_count(int pin) { (void)pin; return 0; }
int hal_gpio_measure_pulse(int trig, int echo, uint32_t pulse, uint32_t timeout)
{ (void)trig; (void)echo; (void)pulse; (void)timeout; return HAL_ERR_TIMEOUT; }
uint64_t hal_time_us(void) { return 0; }
uint32_t hal_time_ms(void) { return 0; }
void hal_delay_ms(uint32_t ms) { (void)ms; }
void hal_delay_us(uint32_t us) { (void)us; }

static app_cfg_t config(int trig, int echo)
{
    app_cfg_t cfg = {0};
    snprintf(cfg.name, sizeof cfg.name, "SONAR");
    cfg.proto = proto_gpio;
    cfg.mode = sample_mode_poll;
    cfg.gpio_mode = "pulse";
    cfg.gpio_trig = trig;
    cfg.gpio_echo = echo;
    cfg.timing.mode = TIMING_MODE_PERIODIC;
    cfg.timing.interval_ms = 50;
    cfg.timing.enabled = true;
    return cfg;
}

static void test_parser(void)
{
    app_cfg_t cfg = config(8, 8);
    cfg_parse_begin();
    cfg_parse_end(&cfg);
    assert(strstr(cfg.cfg_error, "differ"));

    cfg = config(8, 9);
    cfg_parse_begin();
    cfg_parse_end(&cfg);
    assert(!cfg.cfg_error[0]);

    /* A single-pin input remains valid; pulse-only constraints must not leak. */
    cfg.gpio_mode = "digital";
    cfg.gpio_pin = 8;
    cfg.gpio_echo = cfg.gpio_trig;
    cfg_parse_begin();
    cfg_parse_end(&cfg);
    assert(!cfg.cfg_error[0]);
}

static void test_driver(void)
{
    /* Direct callers must be protected even if they bypass parser validation. */
    app_cfg_t cfg = config(8, 8);
    sensor_cfg_t sensor = { .name = cfg.name, .proto = "gpio", .kv_pairs = &cfg };
    void *ctx = NULL;
    mode_calls = 0;
    assert(bus_gpio.init_cfg(&sensor, &ctx) != 0);
    assert(ctx == NULL && mode_calls == 0);

    cfg = config(8, 9);
    assert(bus_gpio.init_cfg(&sensor, &ctx) == 0);
    assert(ctx != NULL && mode_calls == 2);
    bus_gpio.deinit(ctx);

    cfg = config(8, -1);
    mode_calls = 0;
    ctx = NULL;
    assert(bus_gpio.init_cfg(&sensor, &ctx) != 0);
    assert(ctx == NULL && mode_calls == 0);
    cfg = config(8, 49);
    assert(bus_gpio.init_cfg(&sensor, &ctx) != 0);
    assert(ctx == NULL && mode_calls == 0);
}

int main(void)
{
    test_parser();
    test_driver();
    puts("GPIO pulse configuration: PASS (distinct pins, refusal before hardware setup)");
    return 0;
}
