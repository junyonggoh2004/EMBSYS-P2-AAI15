#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bus/bus_common.h"
#include "bus/bus_if.h"
#include "hal/hal.h"
#include "application/scheduler.h"

static unsigned mode_calls;
static unsigned capture_starts, capture_polls, capture_cancels, legacy_calls;
static int capture_start_rc = HAL_ERR_UNSUPPORTED;
static int capture_poll_rc = HAL_PULSE_PENDING;
static hal_pulse_result_t capture_result = {1000, 1580, 580};
static uint64_t now_us;
static unsigned sonar_samples, other_samples;

int hal_gpio_capture_start(int trig, int echo, uint32_t pulse, uint32_t timeout)
{
    (void)trig; (void)echo; (void)pulse; (void)timeout;
    ++capture_starts;
    return capture_start_rc;
}
int hal_gpio_capture_poll(int echo, hal_pulse_result_t *result)
{
    (void)echo;
    ++capture_polls;
    *result = capture_result;
    return capture_poll_rc;
}
void hal_gpio_capture_cancel(int echo) { (void)echo; ++capture_cancels; }

bool hal_gpio_valid(int pin) { return pin >= 0 && pin < 49; }
int hal_gpio_mode(int pin, hal_gpio_mode_t mode)
{ (void)pin; (void)mode; ++mode_calls; return HAL_OK; }
int hal_gpio_read(int pin) { (void)pin; return 0; }
int hal_gpio_write(int pin, int value) { (void)pin; (void)value; return HAL_OK; }
int hal_gpio_count_edges(int pin, bool enable)
{ (void)pin; (void)enable; return HAL_OK; }
uint32_t hal_gpio_edge_count(int pin) { (void)pin; return 0; }
int hal_gpio_measure_pulse(int trig, int echo, uint32_t pulse, uint32_t timeout)
{ (void)trig; (void)echo; (void)pulse; (void)timeout; ++legacy_calls; return HAL_ERR_TIMEOUT; }
uint64_t hal_time_us(void) { return now_us; }
uint32_t hal_time_ms(void) { return (uint32_t)(now_us / 1000); }
void hal_delay_ms(uint32_t ms) { (void)ms; }
void hal_delay_us(uint32_t us) { (void)us; }
const bus_t *bus_lookup(const char *name) { (void)name; return &bus_gpio; }
bool mqtt_follow_remote_source(const char *node, const char *source, const char *alias)
{ (void)node; (void)source; (void)alias; return true; }
bool mqtt_pub_bytes(const char *source, const uint8_t *data, size_t len, int qos, bool retain)
{ (void)source; (void)data; (void)len; (void)qos; (void)retain; return true; }
bool mqtt_pub_line(const char *source, const char *line, int qos, bool retain)
{ (void)source; (void)line; (void)qos; (void)retain; return true; }
void rules_on_sample(const char *source, const uint8_t *data, int len, uint32_t ts)
{
    (void)ts;
    if (!strcmp(source, "SONAR")) {
        assert(len == 3 && data[2] == 1);
        ++sonar_samples;
    } else ++other_samples;
}

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

static void test_async_lifecycle(void)
{
    app_cfg_t cfg = config(8, 9);
    sensor_cfg_t sensor = {cfg.name, "gpio", &cfg};
    void *ctx = NULL;
    uint8_t buf[3] = {0};
    assert(bus_gpio.init_cfg(&sensor, &ctx) == 0);
    assert(bus_gpio.probe(ctx) == 0 && capture_starts == 0 && legacy_calls == 0);
    capture_start_rc = HAL_OK;
    assert(bus_gpio.poll_once(ctx, buf, 2) < 0 && capture_starts == 0);
    assert(bus_gpio.poll_once(ctx, buf, sizeof buf) == BUS_POLL_PENDING);
    assert(bus_gpio.poll_once(ctx, buf, sizeof buf) == BUS_POLL_PENDING);
    assert(capture_starts == 1 && capture_polls == 1 && legacy_calls == 0);
    capture_poll_rc = HAL_OK;
    assert(bus_gpio.poll_once(ctx, buf, sizeof buf) == 3);
    assert(buf[0] == 0x44 && buf[1] == 2 && buf[2] == 1);
    capture_poll_rc = HAL_ERR_TIMEOUT;
    assert(bus_gpio.poll_once(ctx, buf, sizeof buf) == BUS_POLL_PENDING);
    memset(buf, 0xAA, sizeof buf);
    assert(bus_gpio.poll_once(ctx, buf, sizeof buf) == HAL_ERR_TIMEOUT);
    assert(buf[2] == 0xAA); // error never emits valid-distance bytes
    capture_start_rc = HAL_ERR_BUSY;
    assert(bus_gpio.poll_once(ctx, buf, sizeof buf) == HAL_ERR_BUSY);
    assert(legacy_calls == 0);
    capture_start_rc = HAL_OK;
    assert(bus_gpio.poll_once(ctx, buf, sizeof buf) == BUS_POLL_PENDING);
    unsigned cancelled = capture_cancels;
    bus_gpio.cancel(ctx);
    bus_gpio.cancel(ctx);
    assert(capture_cancels == cancelled + 1);
    assert(bus_gpio.poll_once(ctx, buf, sizeof buf) == BUS_POLL_PENDING);
    bus_gpio.deinit(ctx);
    assert(capture_cancels == cancelled + 2); // CLEAR/deinit releases capture

    assert(bus_gpio.init_cfg(&sensor, &ctx) == 0);
    capture_start_rc = HAL_ERR_UNSUPPORTED;
    assert(bus_gpio.poll_once(ctx, buf, sizeof buf) == 1 && buf[0] == 0);
    assert(legacy_calls == 1); // explicit baseline compatibility
    bus_gpio.deinit(ctx);
}

static void test_scheduler_with_real_gpio(void)
{
    scheduler_reset();
    now_us = 1000000;
    capture_start_rc = HAL_OK;
    capture_poll_rc = HAL_PULSE_PENDING;
    app_cfg_t sonar = config(8, 9);
    sonar.timing.timeout_ms = 20;
    app_cfg_t other = config(10, 11);
    snprintf(other.name, sizeof other.name, "OTHER");
    other.gpio_mode = "digital";
    other.gpio_pin = 10;
    assert(scheduler_add_config(&sonar) == 0);
    assert(scheduler_add_config(&other) == 1);
    unsigned starts_before = capture_starts, cancelled = capture_cancels;
    scheduler_run_all(true);
    assert(now_us == 1000000 && sonar_samples == 0 && other_samples == 1);
    assert(capture_starts == starts_before + 1);
    app_cfg_t conflict = other;
    conflict.gpio_pin = 9;
    unsigned modes_before = mode_calls;
    assert(scheduler_add_config(&conflict) < 0 && mode_calls == modes_before);
    conflict = config(12, 8);
    assert(scheduler_add_config(&conflict) < 0 && mode_calls == modes_before);
    conflict.proto = proto_spi;
    conflict.spi_sclk = 9;
    assert(scheduler_add_config(&conflict) < 0 && mode_calls == modes_before);
    assert(scheduler_count() == 2); // pending observation and other sensor preserved
    assert(scheduler_next_due_ms(true, 1000) == 1);
    now_us += 1000;
    capture_poll_rc = HAL_OK;
    scheduler_run_all(true);
    assert(sonar_samples == 1 && capture_starts == starts_before + 1);
    now_us = 1050000;
    capture_poll_rc = HAL_PULSE_PENDING;
    scheduler_run_all(true);
    assert(other_samples == 2 && sonar_samples == 1);
    now_us += 20001;
    scheduler_run_all(true);
    assert(sonar_samples == 1 && capture_cancels == cancelled + 2);
    now_us = 1100000;
    scheduler_run_all(true);
    scheduler_run_all(false);
    assert(capture_cancels == cancelled + 3);
    scheduler_run_all(true);
    scheduler_reset();
    assert(capture_cancels == cancelled + 4); // CLEAR releases real GPIO context
}

static void test_scheduler_pair_diagnostic(void)
{
    scheduler_reset();
    now_us = 1000000;
    capture_start_rc = HAL_OK;
    capture_poll_rc = HAL_PULSE_PENDING;
    app_cfg_t a = config(8, 9), b = config(10, 11);
    snprintf(a.name, sizeof a.name, "A");
    snprintf(b.name, sizeof b.name, "B");
    a.gpio_pulse_timeout_us = b.gpio_pulse_timeout_us = 20000;
    assert(scheduler_add_config(&a) == 0 && scheduler_add_config(&b) == 1);
    unsigned starts = capture_starts, cancelled = capture_cancels;
    unsigned samples = sonar_samples + other_samples;
    assert(scheduler_pair_command("A B A 10001") == HAL_ERR_INVALID);
    assert(scheduler_pair_command("A B A -1") == HAL_ERR_INVALID);
    assert(scheduler_pair_command("A B A 10000 trailing") == HAL_ERR_INVALID);
    assert(scheduler_pair_command("A A A 10000") == HAL_ERR_INVALID);
    assert(scheduler_pair_command("A missing A 10000") == HAL_ERR_INVALID);
    assert(capture_starts == starts);
    assert(scheduler_pair_command("A B A 10000") == HAL_OK);
    assert(scheduler_pair_command("A B B 10000") == HAL_ERR_BUSY);
    assert(scheduler_next_due_ms(false, 1000) == 0);
    scheduler_run_all(false);
    assert(capture_starts == starts + 1 && scheduler_next_due_ms(false, 1000) == 1);
    capture_result = (hal_pulse_result_t){1000100, 1000680, 580};
    capture_poll_rc = HAL_OK;
    now_us = 1001000;
    scheduler_run_all(false);
    assert(scheduler_next_due_ms(false, 1000) == 10);
    now_us = 1010999;
    scheduler_run_all(false);
    assert(capture_starts == starts + 1);
    now_us = 1011000;
    scheduler_run_all(false);
    capture_result = (hal_pulse_result_t){1011100, 1011680, 580};
    now_us = 1012000;
    scheduler_run_all(false);
    assert(capture_starts == starts + 2 && capture_cancels == cancelled + 2);
    assert(sonar_samples + other_samples == samples); // no raw/occupancy producer handoff
    assert(scheduler_next_due_ms(false, 1000) == 1000);
    scheduler_run_all(false);
    assert(capture_starts == starts + 2); // exactly one pair, no automatic repeat

    assert(scheduler_pair_command("A B B 10000") == HAL_OK);
    scheduler_run_all(false);
    scheduler_pair_cancel(); // STOP
    scheduler_run_all(false);
    assert(capture_cancels == cancelled + 3);
    assert(scheduler_pair_command("A B A 10000") == HAL_OK);
    scheduler_run_all(false);
    scheduler_reset(); // CLEAR before contexts are freed
    assert(capture_cancels == cancelled + 4);
}

int main(void)
{
    test_parser();
    test_driver();
    test_async_lifecycle();
    test_scheduler_with_real_gpio();
    test_scheduler_pair_diagnostic();
    puts("GPIO pulse configuration/lifecycle: PASS (real scheduler/driver, async service, timeout, STOP/CLEAR)");
    return 0;
}
