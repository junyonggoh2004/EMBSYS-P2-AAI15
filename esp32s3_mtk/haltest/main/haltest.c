// haltest.c
// HAL test firmware for the µT-Kernel build (D1 task 27), driven by
// parity/hal_test.py. It runs the same checks as the Arduino HAL test sketch
// (esp32s3_haltest/), with the same output format, on the µT-Kernel HAL, plus
// checks that need no wiring: an output pin with its input enabled reads its
// own level back, so PWM duty, PULSE width and interrupt edge counting can be
// measured on one pin, and the UART has an internal loopback.
//
// Console commands: SELFTEST runs the checks; any other line is echoed back as
// "[ECHO] <line>". A "[TICK] <ms>" line is printed every 1000 ms.
//
// The test runs in one µT-Kernel task pinned to core 1 (where the firmware's
// SchedulerTask registers the GPIO interrupt), with TA_FPU because it prints
// and compares floats.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <tk/tkernel.h>
#include "esp_cpu.h"

#include "hal/hal.h"
#include "edge_mtk.h"

static int s_fail;

static void check(const char *name, bool ok, const char *detail)
{
    printf("[HALTEST] %s %s %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) s_fail++;
}

// Fraction of n samples that read high, on a pin whose input sees the signal.
static float duty_of(int pin, uint32_t n)
{
    uint32_t high = 0;
    for (uint32_t i = 0; i < n; i++) high += hal_gpio_read(pin) > 0;
    return (float)high / (float)n;
}

static void pwm_duty_checks(int out_pin, int in_pin, const char *suffix)
{
    static const float duties[] = { 0.25f, 0.50f, 0.75f };
    char d[64], name[32];
    for (unsigned i = 0; i < sizeof duties / sizeof duties[0]; i++) {
        hal_pwm_set(out_pin, duties[i]);
        hal_delay_ms(5);
        float measured = duty_of(in_pin, 40000);
        snprintf(name, sizeof name, "pwm-duty-%02d%s", (int)(duties[i] * 100 + 0.5f), suffix);
        snprintf(d, sizeof d, "set %.2f measured %.3f", (double)duties[i], (double)measured);
        check(name, fabsf(measured - duties[i]) <= 0.03f, d);
    }
    hal_pwm_stop(out_pin);
}

static void pulse_check(int out_pin, int in_pin, const char *name)
{
    char d[64];
    uint64_t t0 = hal_time_us();
    int pr = hal_gpio_pulse(out_pin, 200);
    uint64_t ret_us = hal_time_us() - t0;
    while (hal_gpio_read(in_pin) <= 0 && hal_time_us() - t0 < 50000) {}
    uint64_t rise = hal_time_us();
    while (hal_gpio_read(in_pin) > 0 && hal_time_us() - t0 < 400000) tk_dly_tsk(1);
    uint64_t width = hal_time_us() - rise;
    snprintf(d, sizeof d, "returned after %llu us, high %llu us",
             (unsigned long long)ret_us, (unsigned long long)width);
    check(name, pr == HAL_OK && ret_us < 1000 && width >= 199000 && width <= 202000, d);
}

static void edge_count_check(int out_pin, int in_pin, const char *name)
{
    char d[80];
    if (in_pin != out_pin) hal_gpio_mode(in_pin, HAL_GPIO_INPUT_PULLDOWN);
    hal_gpio_mode(out_pin, HAL_GPIO_OUTPUT);
    int er = hal_gpio_count_edges(in_pin, true);
    const uint32_t edges = 20000;
    uint64_t te = hal_time_us();
    for (uint32_t i = 0; i < edges; i++) {
        hal_gpio_write(out_pin, (i & 1) ? 0 : 1);
        hal_delay_us(50);
    }
    uint64_t span = hal_time_us() - te;
    hal_delay_ms(2);
    uint32_t got = hal_gpio_edge_count(in_pin);
    hal_gpio_count_edges(in_pin, false);
    snprintf(d, sizeof d, "%lu edges at %.1f kHz, %lu counted (er=%d)", (unsigned long)edges,
             edges * 1000.0 / (double)span, (unsigned long)got, er);
    check(name, er == HAL_OK && got == edges, d);
}

static void run_selftest(void)
{
    char d[96];
    s_fail = 0;
    printf("[HALTEST] start\n");

    uint64_t a = hal_time_us();
    uint64_t b = hal_time_us();
    snprintf(d, sizeof d, "%llu -> %llu us", (unsigned long long)a, (unsigned long long)b);
    check("time-monotonic", b >= a, d);

    uint64_t us = hal_time_us();
    uint32_t ms = hal_time_ms();
    snprintf(d, sizeof d, "ms=%lu us/1000=%llu", (unsigned long)ms, (unsigned long long)(us / 1000));
    check("time-ms-matches-us", ms >= us / 1000 && ms - us / 1000 <= 1, d);

    uint64_t t0 = hal_time_us();
    hal_delay_ms(100);
    uint64_t dt = hal_time_us() - t0;
    snprintf(d, sizeof d, "%llu us", (unsigned long long)dt);
    check("delay-ms-100", dt >= 100000 && dt <= 102000, d);

    t0 = hal_time_us();
    hal_delay_ms(1);
    dt = hal_time_us() - t0;
    snprintf(d, sizeof d, "%llu us", (unsigned long long)dt);
    check("delay-ms-1", dt >= 1000 && dt <= 2100, d);

    t0 = hal_time_us();
    hal_delay_us(500);
    dt = hal_time_us() - t0;
    snprintf(d, sizeof d, "%llu us", (unsigned long long)dt);
    check("delay-us-500", dt >= 500 && dt <= 600, d);

    t0 = hal_time_us();
    int ch = hal_console_getc(20000);
    dt = hal_time_us() - t0;
    snprintf(d, sizeof d, "ret=%d after %llu us", ch, (unsigned long long)dt);
    check("console-getc-timeout", ch == HAL_ERR_TIMEOUT && dt >= 20000, d);

    static const int reserved[] = { -1, 19, 20, 22, 26, 32, 49, 99 };
    bool all = true;
    for (unsigned i = 0; i < sizeof reserved / sizeof reserved[0]; i++) {
        int p = reserved[i];
        all = all && !hal_gpio_valid(p) && hal_gpio_mode(p, HAL_GPIO_OUTPUT) == HAL_ERR_PIN
                  && hal_gpio_write(p, 1) == HAL_ERR_PIN && hal_gpio_read(p) == HAL_ERR_PIN
                  && hal_gpio_toggle(p) == HAL_ERR_PIN && hal_pwm_set(p, 0.5f) == HAL_ERR_PIN;
    }
    check("gpio-reserved-pins-refused", all, "-1 19 20 22 26 32 49 99");

    int r = hal_gpio_mode(4, HAL_GPIO_INPUT_PULLDOWN);
    int lvl = hal_gpio_read(4);
    snprintf(d, sizeof d, "mode=%d read=%d", r, lvl);
    check("gpio-input-pulldown", r == HAL_OK && lvl == 0, d);

    r = hal_gpio_mode(4, HAL_GPIO_INPUT_PULLUP);
    lvl = hal_gpio_read(4);
    snprintf(d, sizeof d, "mode=%d read=%d", r, lvl);
    check("gpio-input-pullup", r == HAL_OK && lvl == 1, d);

    r = hal_gpio_mode(4, HAL_GPIO_OUTPUT);
    int t1 = hal_gpio_toggle(4), l1 = hal_gpio_read(4);
    int t2 = hal_gpio_toggle(4), l2 = hal_gpio_read(4);
    snprintf(d, sizeof d, "toggle %d/%d then %d/%d", t1, l1, t2, l2);
    check("gpio-output-toggle", r == HAL_OK && t1 == 1 && l1 == 1 && t2 == 0 && l2 == 0, d);

    int p1 = hal_pwm_set(4, 0.5f), p2 = hal_pwm_set(4, 0.25f), p3 = hal_pwm_stop(4);
    snprintf(d, sizeof d, "set=%d set=%d stop=%d read=%d", p1, p2, p3, hal_gpio_read(4));
    check("pwm-set-and-stop", p1 == HAL_OK && p2 == HAL_OK && p3 == HAL_OK && hal_gpio_read(4) == 0, d);

    // Wiring-free checks on GPIO4: the output's own level is read back.
    pwm_duty_checks(4, 4, "-self");
    pulse_check(4, 4, "pulse-200ms-nonblocking-self");
    edge_count_check(4, 4, "edge-count-no-loss-self");
    hal_gpio_mode(4, HAL_GPIO_INPUT);

    // UART through the controller's internal loopback (TX pin 4, RX pin 5).
    int port = hal_uart_open(4, 5, 115200, 8, 'N', 1);
    if (port >= 0) {
        hal_mtk_uart_internal_loopback(port, true);
        hal_delay_ms(2);                      // switching to loopback can produce one stray frame
        while (hal_uart_read_byte(port) >= 0) {}
        static const char msg[] = "uT-Kernel UART loopback 0123456789";
        hal_uart_write(port, (const uint8_t *)msg, sizeof msg - 1);
        char got[64];
        size_t n = 0;
        uint64_t te = hal_time_us() + 50000;
        while (n < sizeof msg - 1 && hal_time_us() < te) {
            int c = hal_uart_read_byte(port);
            if (c >= 0) got[n++] = (char)c; else tk_dly_tsk(1);
        }
        got[n] = 0;
        hal_mtk_uart_internal_loopback(port, false);
        snprintf(d, sizeof d, "port=%d got %u/%u bytes", port, (unsigned)n, (unsigned)(sizeof msg - 1));
        check("uart-internal-loopback", n == sizeof msg - 1 && memcmp(got, msg, n) == 0, d);
    } else {
        snprintf(d, sizeof d, "hal_uart_open=%d", port);
        check("uart-internal-loopback", false, d);
    }
    hal_gpio_mode(4, HAL_GPIO_INPUT);
    hal_gpio_mode(5, HAL_GPIO_INPUT);

    // I2C with nothing attached (pins 4/5, internal pull-ups): every transfer
    // must fail with HAL_ERR_IO, never a valid-looking result.
    int bus = hal_i2c_open(4, 5, 100000);
    uint8_t reg = 0x0F, buf[6] = { 0 };
    int w = hal_i2c_write(bus, 0x19, &reg, 1, false);
    int w2 = hal_i2c_write(bus, 0x19, &reg, 1, true);
    int rd = hal_i2c_read(bus, 0x19, buf, sizeof buf);
    snprintf(d, sizeof d, "bus=%d write=%d write-nostop=%d read=%d", bus, w, w2, rd);
    check("i2c-missing-device-nack", bus >= 0 && w == HAL_ERR_IO && rd == HAL_ERR_IO, d);
    hal_gpio_mode(4, HAL_GPIO_INPUT);
    hal_gpio_mode(5, HAL_GPIO_INPUT);

    // Measured checks through the jumper between GPIO5 and GPIO6 (the S3 pins
    // for the Pico's GP21->GP22 loopback). Skipped when no jumper is fitted.
    hal_gpio_mode(6, HAL_GPIO_INPUT_PULLDOWN);
    hal_gpio_mode(5, HAL_GPIO_OUTPUT);
    hal_gpio_write(5, 1);
    hal_delay_us(50);
    bool jumper = hal_gpio_read(6) == 1;
    hal_gpio_write(5, 0);
    hal_delay_us(50);
    jumper = jumper && hal_gpio_read(6) == 0;
    if (!jumper) {
        printf("[HALTEST] SKIP jumper-measurements no jumper between GPIO5 and GPIO6\n");
    } else {
        pwm_duty_checks(5, 6, "");
        pulse_check(5, 6, "pulse-200ms-nonblocking");
        edge_count_check(5, 6, "edge-count-no-loss");
    }
    hal_gpio_mode(5, HAL_GPIO_INPUT);
    hal_gpio_mode(6, HAL_GPIO_INPUT);

    snprintf(d, sizeof d, "running on core %d", esp_cpu_get_core_id());
    check("test-task-on-core-1", esp_cpu_get_core_id() == 1, d);

    printf("[HALTEST] done failures=%d\n", s_fail);
}

static void test_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    ER er = hal_mtk_init();
    if (er == E_OK) er = console_start_rx();
    printf("[BOOT] esp32s3_mtk HAL test (micro T-Kernel 3 SMP), init er=%d\n", (int)er);
    run_selftest();

    static char line[128];
    size_t n = 0;
    uint32_t next_tick = hal_time_ms() + 1000;
    for (;;) {
        int ch;
        while ((ch = hal_console_getc(0)) >= 0) {
            if (ch == '\r') continue;
            if (ch != '\n') {
                if (n < sizeof line - 1) line[n++] = (char)ch;
                continue;
            }
            line[n] = 0;
            n = 0;
            if (strcmp(line, "SELFTEST") == 0) run_selftest();
            else printf("[ECHO] %s\n", line);
        }
        if ((int32_t)(hal_time_ms() - next_tick) >= 0) {
            printf("[TICK] %lu\n", (unsigned long)hal_time_ms());
            next_tick += 1000;
        }
        hal_delay_ms(2);
    }
}

static void idle_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    for (;;) asm volatile ("waiti 0" ::: "memory");
}

static ER start(FP entry, PRI pri, SZ stack, UW prc, ATR extra)
{
    T_CTSK c = { .tskatr = TA_HLNG | TA_RNG0 | TA_ASSPRC | extra, .task = entry,
                 .itskpri = pri, .stksz = stack, .assprc = prc };
    ID id = tk_cre_tsk(&c);
    return id < E_OK ? (ER)id : tk_sta_tsk(id, 0);
}

EXPORT INT usermain(void)
{
    ER er = console_init();
    if (er == E_OK) er = start((FP)idle_task, CNF_MAX_TSKPRI, 1024, TP_PRC1, 0);
    if (er == E_OK) er = start((FP)idle_task, CNF_MAX_TSKPRI, 1024, TP_PRC2, 0);
    if (er == E_OK) er = start((FP)test_task, 21, 8192, TP_PRC2, TA_FPU);
    if (er != E_OK) printf("[BOOT] start-up failed er=%d\n", (int)er);
    tk_slp_tsk(TMO_FEVR);
    return 0;
}
