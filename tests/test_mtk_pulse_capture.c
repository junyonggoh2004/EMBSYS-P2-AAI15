/* Actual asynchronous MTK HAL functions; only kernel/register/clock
 * boundaries are fakes. Edge IRQ timestamps are injected explicitly. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hal/hal.h"
#include "application/pulse_sequence.h"

typedef int ID;
enum { GPIO_INTR_POSEDGE = 1 };
static void *s_gpio;
static unsigned s_gpio_core = 1;
static bool s_gpio_irq = true;
static ID s_echo_flg = 1;
static int s_echo_pin = -1;
static uint64_t s_echo_rise_us, s_echo_fall_us, s_count_mask;
static uint64_t now_us;
static int core = 2, levels[49];
static bool irq_enabled;
static unsigned triggers, clears;

static ID tk_get_prc(void) { return core; }
static void tk_clr_flg(ID flag, unsigned pattern) { (void)flag; (void)pattern; }
bool hal_gpio_valid(int pin) { return pin >= 0 && pin < 49; }
int hal_gpio_mode(int pin, hal_gpio_mode_t mode)
{ (void)mode; levels[pin] = 0; return HAL_OK; }
uint64_t hal_time_us(void) { return now_us; }
static void gpio_ll_set_level(void *gpio, int pin, int level)
{ (void)gpio; levels[pin] = level; if (level) ++triggers; }
static int gpio_ll_get_level(void *gpio, int pin) { (void)gpio; return levels[pin]; }
static void gpio_ll_intr_disable(void *gpio, int pin)
{ (void)gpio; (void)pin; irq_enabled = false; }
static void gpio_ll_intr_enable_on_core(void *gpio, unsigned owner, int pin)
{ (void)gpio; (void)owner; (void)pin; irq_enabled = true; }
static void gpio_ll_set_intr_type(void *gpio, int pin, int type)
{ (void)gpio; (void)pin; (void)type; }
static void esp_rom_delay_us(unsigned us) { now_us += us; }
static void echo_clear_status(int pin) { (void)pin; ++clears; }
static void echo_disarm(int pin)
{ gpio_ll_intr_disable(s_gpio, pin); s_echo_pin = -1; }

#include "../esp32s3_mtk/components/edge_mtk/pulse_capture_mtk.h"

static void reset(void)
{
    core = 2;
    if (s_capture_active) hal_gpio_capture_cancel(s_echo_pin);
    now_us = 1000000;
    memset(levels, 0, sizeof levels);
    s_count_mask = 0;
    triggers = clears = 0;
    s_gpio_irq = true;
}

static void test_start_and_conflict(void)
{
    reset();
    assert(hal_gpio_capture_start(8, 8, 10, 30000) == HAL_ERR_INVALID);
    assert(hal_gpio_capture_start(-1, 9, 10, 30000) == HAL_ERR_PIN);
    assert(hal_gpio_capture_start(8, 9, 1001, 30000) == HAL_ERR_INVALID);
    assert(hal_gpio_capture_start(8, 9, 10, 10) == HAL_ERR_INVALID);
    levels[9] = 1;
    assert(hal_gpio_capture_start(8, 9, 10, 30000) == HAL_ERR_BUSY);
    levels[9] = 0;
    s_count_mask = 1ULL << 9;
    assert(hal_gpio_capture_start(8, 9, 10, 30000) == HAL_ERR_BUSY);
    s_count_mask = 0;
    s_gpio_irq = false;
    assert(hal_gpio_capture_start(8, 9, 10, 30000) == HAL_ERR_IO);
    s_gpio_irq = true;
    core = 1;
    assert(hal_gpio_capture_start(8, 9, 10, 30000) == HAL_ERR_IO);
    core = 2;
    assert(triggers == 0);
    assert(hal_gpio_capture_start(8, 9, 10, 30000) == HAL_OK);
    assert(now_us == 1000010 && triggers == 1 && !levels[8]);
    assert(hal_gpio_capture_start(10, 11, 10, 30000) == HAL_ERR_BUSY);
    hal_gpio_capture_cancel(11);
    assert(s_capture_active && s_echo_pin == 9 && irq_enabled);
}

static void test_completion_and_timeout(void)
{
    reset();
    hal_pulse_result_t result;
    assert(hal_gpio_capture_start(8, 9, 10, 30000) == HAL_OK);
    assert(hal_gpio_capture_poll(11, &result) == HAL_ERR_INVALID);
    assert(hal_gpio_capture_poll(9, &result) == HAL_PULSE_PENDING);
    assert(now_us == 1000010 && irq_enabled);
    s_echo_rise_us = 1000100;
    now_us = 1000200;
    assert(hal_gpio_capture_poll(9, &result) == HAL_PULSE_PENDING);
    s_echo_fall_us = 1000680;
    now_us = 1040000; // delayed service may still read an on-time capture
    assert(hal_gpio_capture_poll(9, &result) == HAL_OK);
    assert(result.width_us == 580 && result.rise_us == 1000100);
    assert(!s_capture_active && !irq_enabled && s_echo_pin == -1);
    assert(hal_gpio_capture_poll(9, &result) == HAL_ERR_INVALID);

    for (unsigned rise = 0; rise < 2; ++rise) {
        reset();
        assert(hal_gpio_capture_start(8, 9, 10, 30000) == HAL_OK);
        if (rise) s_echo_rise_us = 1000100; // missing fall / stuck high
        now_us = 1030000;
        assert(hal_gpio_capture_poll(9, &result) == HAL_ERR_TIMEOUT);
        assert(!result.width_us && !s_capture_active && !irq_enabled);
    }
    reset();
    assert(hal_gpio_capture_start(8, 9, 10, 30000) == HAL_OK);
    s_echo_rise_us = 1000100;
    s_echo_fall_us = 1030001; // late edge never valid
    assert(hal_gpio_capture_poll(9, &result) == HAL_ERR_TIMEOUT);
    assert(!result.width_us);
}

static void test_repeated_cancel(void)
{
    reset();
    for (unsigned i = 0; i < 100; ++i) {
        assert(hal_gpio_capture_start(8, 9, 10, 30000) == HAL_OK);
        hal_gpio_capture_cancel(9);
        assert(!s_capture_active && !irq_enabled && !levels[8]);
        unsigned before = clears;
        hal_gpio_capture_cancel(9);
        assert(clears == before);
    }
    assert(triggers == 100);
}

static int sequence_start(void *context, unsigned sensor, uint32_t timeout)
{
    (void)context;
    return hal_gpio_capture_start(8 + (int)sensor * 2, 9 + (int)sensor * 2, 10, timeout);
}
static int sequence_poll(void *context, unsigned sensor, hal_pulse_result_t *pulse)
{
    (void)context;
    return hal_gpio_capture_poll(9 + (int)sensor * 2, pulse);
}
static void sequence_cancel(void *context, unsigned sensor)
{
    (void)context;
    hal_gpio_capture_cancel(9 + (int)sensor * 2);
}

static void test_sequence_with_actual_capture(void)
{
    const pulse_sequence_config_t config = {
        .first = 0, .timeout_us = {20000, 20000}, .guard_us = 10000,
        .window_us = 50000, .error_policy = PULSE_SEQUENCE_FAIL_FAST
    }; // timing values are fixtures, not an approved physical guard
    const pulse_sequence_ops_t ops = {sequence_start, sequence_poll, sequence_cancel};
    pulse_sequence_t s = {0};
    reset();
    assert(pulse_sequence_begin(&s, &config, &ops, NULL, now_us) == HAL_OK);
    assert(pulse_sequence_step(&s, now_us) == PULSE_SEQUENCE_WAIT);
    assert(triggers == 1 && s_echo_pin == 9);
    s_echo_rise_us = 1000100;
    s_echo_fall_us = 1001100;
    now_us = 1002000;
    assert(pulse_sequence_step(&s, now_us) == PULSE_SEQUENCE_GUARD);
    assert(!s_capture_active && s_echo_pin == -1);
    now_us = 1011999;
    assert(pulse_sequence_step(&s, now_us) == PULSE_SEQUENCE_GUARD && triggers == 1);
    now_us = 1012000;
    assert(pulse_sequence_step(&s, now_us) == PULSE_SEQUENCE_WAIT);
    assert(triggers == 2 && s_echo_pin == 11);
    s_echo_rise_us = 1012100;
    s_echo_fall_us = 1013100;
    now_us = 1014000;
    assert(pulse_sequence_step(&s, now_us) == PULSE_SEQUENCE_COMPLETE);
    assert(s.samples[0].valid && s.samples[1].valid && !s_capture_active);

    reset();
    assert(pulse_sequence_begin(&s, &config, &ops, NULL, now_us) == HAL_OK);
    pulse_sequence_step(&s, now_us);
    now_us = 1020001;
    assert(pulse_sequence_step(&s, now_us) == PULSE_SEQUENCE_FAILED);
    assert(!s_capture_active && !irq_enabled && triggers == 1);

    reset();
    assert(hal_gpio_capture_start(12, 13, 10, 20000) == HAL_OK);
    assert(pulse_sequence_begin(&s, &config, &ops, NULL, now_us) == HAL_OK);
    assert(pulse_sequence_step(&s, now_us) == PULSE_SEQUENCE_FAILED);
    assert(s.samples[0].status == HAL_ERR_BUSY);
    assert(s_capture_active && s_echo_pin == 13); // unrelated owner untouched
    hal_gpio_capture_cancel(13);
}

int main(void)
{
    test_start_and_conflict();
    test_completion_and_timeout();
    test_repeated_cancel();
    test_sequence_with_actual_capture();
    puts("MTK pulse capture: PASS (actual HAL functions, mocked register/clock boundaries)");
}
