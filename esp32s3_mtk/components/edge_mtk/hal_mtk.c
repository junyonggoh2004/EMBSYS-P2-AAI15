// hal_mtk.c
// ESP32-S3 backend for hal/hal.h on µT-Kernel 3 SMP (D1 task 27).
//
// Replaces the Arduino-ESP32 backend (libraries/EdgeS3/src/hal_esp32s3.cpp)
// with the same behaviour. Following the course port's rule for drivers, the
// peripherals are driven at register level through IDF's hal/*_ll.h headers
// and never through esp_driver_* components, which own interrupts and block
// on FreeRTOS primitives. Kernel services are µT-Kernel only:
//
//   delays             tk_dly_tsk, finished with a short busy wait
//   PULSE              a pool of alarm handlers (tk_cre_alm) end the pulse
//   edge counting      one GPIO interrupt handler (tk_def_int) counts edges
//   HC-SR04 echo       the same handler timestamps the echo edges and sets an
//                      event flag; the measuring task sleeps on tk_wai_flg
//   UART receive       one interrupt handler per UART (tk_def_int) fills a ring
//
// Interrupts are registered by hal_mtk_init() and hal_uart_open(), which the
// runtime calls from SchedulerTask, so tk_def_int allocates them on core 1,
// the core that samples (MTK_REDESIGN.md section 4). Handlers never touch
// application data: each writes only its own counters or ring head, which a
// task reads with atomic loads.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <tk/tkernel.h>
#include "esp_cpu.h"
#include "esp_private/periph_ctrl.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "hal/gpio_ll.h"
#include "hal/i2c_ll.h"
#include "hal/ledc_ll.h"
#include "hal/spi_ll.h"
#include "hal/uart_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/interrupts.h"
#include "soc/io_mux_reg.h"

#include "hal/hal.h"
#include "edge_mtk.h"

#define NPINS 49
#define XTAL_HZ 40000000u
#define APB_HZ  80000000u

static gpio_dev_t *const s_gpio = &GPIO;

// ---------- time ----------
// esp_timer_get_time() reads the 64-bit SYSTIMER counter that the kernel tick
// also runs from (no RTOS involved, ISR-safe).

uint64_t hal_time_us(void) { return (uint64_t)esp_timer_get_time(); }
uint32_t hal_time_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// ---------- delay ----------

void hal_delay_ms(uint32_t ms)
{
    // tk_dly_tsk counts 1 ms ticks and may return part of a tick early; the
    // last stretch is finished on the timer so the caller waits at least ms.
    int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    if (ms > 1 && tk_dly_tsk((TMO)(ms - 1)) != E_OK) {
        // not in task context (before the kernel or in a handler): busy-wait
    }
    for (;;) {
        int64_t left = end - esp_timer_get_time();
        if (left <= 0) return;
        if (left > 1000 && tk_dly_tsk(1) == E_OK) continue;
        esp_rom_delay_us(left > 1000 ? 1000 : (uint32_t)left);
    }
}

void hal_delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

// ---------- GPIO ----------

bool hal_gpio_valid(int pin)
{
    if (pin < 0 || pin >= NPINS) return false;
    if (pin >= 22 && pin <= 25) return false;   // not present on the ESP32-S3
    if (pin == 19 || pin == 20) return false;   // native USB, which carries the console
    if (pin >= 26 && pin <= 32) return false;   // SPI flash and the in-package PSRAM
    return true;
}

static int8_t s_pwm_ch[NPINS];      // LEDC channel + 1 driving the pin, 0 if none
static void pwm_release(int pin);

// Plain GPIO function with the pad connected to the GPIO output register.
static void pad_gpio(int pin)
{
    gpio_ll_func_sel(s_gpio, (uint8_t)pin, PIN_FUNC_GPIO);
    esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
    gpio_ll_od_disable(s_gpio, pin);
}

int hal_gpio_mode(int pin, hal_gpio_mode_t mode)
{
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    if (s_pwm_ch[pin]) pwm_release(pin);
    pad_gpio(pin);
    gpio_ll_input_enable(s_gpio, pin);
    switch (mode) {
    case HAL_GPIO_OUTPUT:
        gpio_ll_set_level(s_gpio, pin, 0);
        gpio_ll_output_enable(s_gpio, pin);   // input stays enabled, so the level reads back
        break;
    case HAL_GPIO_INPUT_PULLUP:
        gpio_ll_output_disable(s_gpio, pin);
        gpio_ll_pulldown_dis(s_gpio, pin);
        gpio_ll_pullup_en(s_gpio, pin);
        break;
    case HAL_GPIO_INPUT_PULLDOWN:
        gpio_ll_output_disable(s_gpio, pin);
        gpio_ll_pullup_dis(s_gpio, pin);
        gpio_ll_pulldown_en(s_gpio, pin);
        break;
    default:
        gpio_ll_output_disable(s_gpio, pin);
        gpio_ll_pullup_dis(s_gpio, pin);
        gpio_ll_pulldown_dis(s_gpio, pin);
        break;
    }
    // A pull change takes 1-3 us to move the pad (measured on the HW-747);
    // wait so the next read sees the new level.
    if (mode != HAL_GPIO_OUTPUT) esp_rom_delay_us(10);
    return HAL_OK;
}

int hal_gpio_write(int pin, int level)
{
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    gpio_ll_set_level(s_gpio, pin, level ? 1 : 0);
    return HAL_OK;
}

int hal_gpio_read(int pin)
{
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    return gpio_ll_get_level(s_gpio, pin) ? 1 : 0;
}

int hal_gpio_toggle(int pin)
{
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    int next = !gpio_ll_get_level(s_gpio, pin);
    if (s_pwm_ch[pin]) pwm_release(pin);
    pad_gpio(pin);
    gpio_ll_input_enable(s_gpio, pin);
    gpio_ll_set_level(s_gpio, pin, (uint32_t)next);
    gpio_ll_output_enable(s_gpio, pin);
    return next;
}

// ---------- PULSE: ended by a µT-Kernel alarm handler ----------

#define PULSE_SLOTS 8

typedef struct {
    ID           alm;
    volatile int pin;      // -1 when free
} pulse_slot_t;

static pulse_slot_t s_pulse[PULSE_SLOTS];

// Task-independent context (the alarm callback). Only drives the pin low.
static void pulse_end(void *exinf)
{
    pulse_slot_t *s = (pulse_slot_t *)exinf;
    int pin = s->pin;
    if (pin >= 0) gpio_ll_set_level(s_gpio, pin, 0);
    __atomic_store_n(&s->pin, -1, __ATOMIC_RELEASE);
}

int hal_gpio_pulse(int pin, uint32_t ms)
{
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    pulse_slot_t *slot = NULL;
    for (int i = 0; i < PULSE_SLOTS && !slot; i++)
        if (s_pulse[i].alm > 0 && __atomic_load_n(&s_pulse[i].pin, __ATOMIC_ACQUIRE) == pin) slot = &s_pulse[i];
    for (int i = 0; i < PULSE_SLOTS && !slot; i++)
        if (s_pulse[i].alm > 0 && __atomic_load_n(&s_pulse[i].pin, __ATOMIC_ACQUIRE) < 0) slot = &s_pulse[i];
    if (!slot) return HAL_ERR_UNSUPPORTED;

    tk_stp_alm(slot->alm);                     // a running pulse on this pin restarts
    hal_gpio_mode(pin, HAL_GPIO_OUTPUT);
    __atomic_store_n(&slot->pin, pin, __ATOMIC_RELEASE);
    gpio_ll_set_level(s_gpio, pin, 1);
    if (tk_sta_alm(slot->alm, (RELTIM)(ms ? ms : 1)) != E_OK) {
        gpio_ll_set_level(s_gpio, pin, 0);
        __atomic_store_n(&slot->pin, -1, __ATOMIC_RELEASE);
        return HAL_ERR_UNSUPPORTED;
    }
    return HAL_OK;
}

// ---------- GPIO interrupt: edge counting ----------

static volatile uint32_t s_edges[NPINS];
static volatile uint64_t s_count_mask;      // pins whose edges are counted
static uint32_t s_gpio_core;                 // core the GPIO interrupt is routed to
static bool s_gpio_irq;

// HC-SR04 echo capture (task 29): the handler records when the echo pin rose
// and fell and sets ECHO_RISE / ECHO_FALL; only hal_gpio_measure_pulse(),
// called from SchedulerTask, arms it.
#define ECHO_RISE 0x1u
#define ECHO_FALL 0x2u
static ID s_echo_flg;
static volatile int s_echo_pin = -1;
static volatile uint64_t s_echo_rise_us, s_echo_fall_us;

// GPIO interrupt hook for tests; NULL unless installed.
static void (*s_gpio_hook)(uint32_t lo, uint32_t hi);

void hal_mtk_set_gpio_hook(void (*hook)(uint32_t lo, uint32_t hi))
{
    s_gpio_hook = hook;
}

uint32_t hal_mtk_gpio_core(void) { return s_gpio_core; }

static void gpio_isr(UINT intno)
{
    (void)intno;
    uint64_t now = (uint64_t)esp_timer_get_time();
    uint32_t lo = 0, hi = 0;
    gpio_ll_get_intr_status(s_gpio, s_gpio_core, &lo);
    gpio_ll_get_intr_status_high(s_gpio, s_gpio_core, &hi);
    gpio_ll_clear_intr_status(s_gpio, lo);
    gpio_ll_clear_intr_status_high(s_gpio, hi);
    uint64_t pending = ((uint64_t)hi << 32) | lo;
    uint64_t count = pending & s_count_mask;
    while (count) {
        int pin = __builtin_ctzll(count);
        count &= count - 1;
        s_edges[pin] = s_edges[pin] + 1;      // only this handler writes it
    }
    int ep = s_echo_pin;
    if (ep >= 0 && ((pending >> ep) & 1)) {
        if (!s_echo_rise_us) {
            s_echo_rise_us = now;
            gpio_ll_set_intr_type(s_gpio, ep, GPIO_INTR_NEGEDGE);
            tk_set_flg(s_echo_flg, ECHO_RISE);
        } else if (!s_echo_fall_us) {
            s_echo_fall_us = now;
            gpio_ll_intr_disable(s_gpio, ep);
            tk_set_flg(s_echo_flg, ECHO_FALL);
        }
    }
    if (s_gpio_hook) s_gpio_hook(lo, hi);
}

int hal_gpio_count_edges(int pin, bool enable)
{
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    if (!s_gpio_irq) return HAL_ERR_UNSUPPORTED;
    if (enable) {
        gpio_ll_intr_disable(s_gpio, pin);
        s_edges[pin] = 0;
        __atomic_or_fetch(&s_count_mask, 1ULL << pin, __ATOMIC_RELEASE);
        gpio_ll_set_intr_type(s_gpio, pin, GPIO_INTR_ANYEDGE);
        gpio_ll_clear_intr_status(s_gpio, pin < 32 ? (1u << pin) : 0);
        gpio_ll_clear_intr_status_high(s_gpio, pin >= 32 ? (1u << (pin - 32)) : 0);
        gpio_ll_intr_enable_on_core(s_gpio, s_gpio_core, pin);
    } else {
        gpio_ll_intr_disable(s_gpio, pin);
        __atomic_and_fetch(&s_count_mask, ~(1ULL << pin), __ATOMIC_RELEASE);
    }
    return HAL_OK;
}

uint32_t hal_gpio_edge_count(int pin)
{
    return hal_gpio_valid(pin) ? __atomic_load_n(&s_edges[pin], __ATOMIC_ACQUIRE) : 0;
}

// ---------- HC-SR04 echo: the interrupt timestamps the edges, the task sleeps ----------

static void echo_clear_status(int pin)
{
    gpio_ll_clear_intr_status(s_gpio, pin < 32 ? (1u << pin) : 0);
    gpio_ll_clear_intr_status_high(s_gpio, pin >= 32 ? (1u << (pin - 32)) : 0);
}

static void echo_disarm(int pin)
{
    gpio_ll_intr_disable(s_gpio, pin);
    s_echo_pin = -1;
}

// The reference busy-wait, used only where the task cannot sleep (no GPIO
// interrupt yet, or called outside a task).
static int measure_pulse_busy(int echo, uint32_t timeout_us, uint64_t t0)
{
    while (gpio_ll_get_level(s_gpio, echo) == 0)
        if (esp_timer_get_time() - t0 > timeout_us) return HAL_ERR_TIMEOUT;
    uint64_t start = esp_timer_get_time();
    while (gpio_ll_get_level(s_gpio, echo))
        if (esp_timer_get_time() - start > timeout_us) break;
    return (int)(esp_timer_get_time() - start);
}

int hal_gpio_measure_pulse(int trig, int echo, uint32_t trig_us, uint32_t timeout_us)
{
    if (!hal_gpio_valid(trig) || !hal_gpio_valid(echo)) return HAL_ERR_TIMEOUT;
    hal_gpio_mode(trig, HAL_GPIO_OUTPUT);           // driven low
    esp_rom_delay_us(2);

    bool irq = s_gpio_irq && s_echo_flg > 0;
    if (irq) {                                       // arm for the rising edge
        gpio_ll_intr_disable(s_gpio, echo);
        s_echo_rise_us = s_echo_fall_us = 0;
        tk_clr_flg(s_echo_flg, 0);
        s_echo_pin = echo;
        gpio_ll_set_intr_type(s_gpio, echo, GPIO_INTR_POSEDGE);
        echo_clear_status(echo);
        gpio_ll_intr_enable_on_core(s_gpio, s_gpio_core, echo);
    }

    gpio_ll_set_level(s_gpio, trig, 1);
    esp_rom_delay_us(trig_us);
    gpio_ll_set_level(s_gpio, trig, 0);
    uint64_t t_trig = (uint64_t)esp_timer_get_time();
    if (!irq) return measure_pulse_busy(echo, timeout_us, t_trig);

    // An echo already high when the trigger ends counts as starting now, as
    // in the reference. The pin's interrupt is masked while this is decided,
    // so the handler cannot record a rise at the same time.
    gpio_ll_intr_disable(s_gpio, echo);
    if (!s_echo_rise_us && gpio_ll_get_level(s_gpio, echo)) {
        s_echo_rise_us = t_trig;
        gpio_ll_set_intr_type(s_gpio, echo, GPIO_INTR_NEGEDGE);
        echo_clear_status(echo);
    }
    if (!s_echo_fall_us) gpio_ll_intr_enable_on_core(s_gpio, s_gpio_core, echo);

    // Sleep until the handler reports the edge or the reference timeout
    // passes; each wait is for the exact time left, rounded up to a tick, and
    // repeated if the kernel returns before that time.
    UINT pat;
    for (;;) {
        uint64_t now = (uint64_t)esp_timer_get_time();
        if (s_echo_rise_us || now - t_trig > timeout_us) break;
        ER er = tk_wai_flg(s_echo_flg, ECHO_RISE, TWF_ORW, &pat,
                           (TMO)((t_trig + timeout_us - now + 999) / 1000));
        if (er == E_CTX) {                           // not in a task: fall back
            echo_disarm(echo);
            return measure_pulse_busy(echo, timeout_us, t_trig);
        }
    }
    uint64_t start = s_echo_rise_us;
    if (!start || start - t_trig > timeout_us) {
        echo_disarm(echo);
        return HAL_ERR_TIMEOUT;                      // no echo
    }
    for (;;) {
        uint64_t now = (uint64_t)esp_timer_get_time();
        if (s_echo_fall_us || now - start > timeout_us) break;
        (void)tk_wai_flg(s_echo_flg, ECHO_FALL, TWF_ORW, &pat, (TMO)((start + timeout_us - now + 999) / 1000));
    }
    uint64_t fall = s_echo_fall_us;
    echo_disarm(echo);
    if (!fall || fall - start > timeout_us) return (int)timeout_us + 1;   // still high: D-07
    return (int)(fall - start);
}

// ---------- PWM: LEDC low-speed timer 0, 8 channels ----------
// The frequency is fixed per target so the parity tests match: the Pico runs
// at 150 MHz / 4 / 1001 = 37.46 kHz with 1/1000 steps; here 10-bit LEDC on
// the 80 MHz APB clock with divider 534/256 gives 37.45 kHz.

#define PWM_BITS     10
#define PWM_FREQ_HZ  37463u

static bool s_ledc_ready;
static int8_t s_ch_pin[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };

static void ledc_setup(void)
{
    ledc_dev_t *hw = LEDC_LL_GET_HW();
    PERIPH_RCC_ATOMIC() {
        ledc_ll_enable_bus_clock(true);
        ledc_ll_enable_reset_reg(true);
        ledc_ll_enable_reset_reg(false);
    }
    ledc_ll_enable_mem_power(true);
    ledc_ll_enable_clock(hw, true);
    ledc_ll_set_slow_clk_sel(hw, LEDC_SLOW_CLK_APB);
    ledc_ll_enable_timer_power(hw, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, true);
    uint32_t div = (uint32_t)((((uint64_t)APB_HZ << 8) + (PWM_FREQ_HZ << PWM_BITS) / 2) / ((uint64_t)PWM_FREQ_HZ << PWM_BITS));
    ledc_ll_set_duty_resolution(hw, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, PWM_BITS);
    ledc_ll_set_clock_divider(hw, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, div);
    ledc_ll_timer_rst(hw, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    ledc_ll_timer_resume(hw, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    ledc_ll_ls_timer_update(hw, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    s_ledc_ready = true;
}

static void pwm_release(int pin)
{
    int ch = s_pwm_ch[pin] - 1;
    s_pwm_ch[pin] = 0;
    if (ch < 0) return;
    ledc_dev_t *hw = LEDC_LL_GET_HW();
    ledc_ll_set_sig_out_en(hw, LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, false);
    ledc_ll_set_idle_level(hw, LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, 0);
    ledc_ll_ls_channel_update(hw, LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
    s_ch_pin[ch] = -1;
    esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
}

int hal_pwm_set(int pin, float duty)
{
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    if (!s_ledc_ready) ledc_setup();
    ledc_dev_t *hw = LEDC_LL_GET_HW();

    int ch = s_pwm_ch[pin] - 1;
    if (ch < 0) {
        for (int c = 0; c < 8 && ch < 0; c++) if (s_ch_pin[c] < 0) ch = c;
        if (ch < 0) return HAL_ERR_UNSUPPORTED;
        ledc_channel_t lc = (ledc_channel_t)ch;
        ledc_ll_enable_channel_power(hw, LEDC_LOW_SPEED_MODE, lc, true);
        ledc_ll_bind_channel_timer(hw, LEDC_LOW_SPEED_MODE, lc, LEDC_TIMER_0);
        ledc_ll_set_hpoint(hw, LEDC_LOW_SPEED_MODE, lc, 0);
        ledc_ll_set_idle_level(hw, LEDC_LOW_SPEED_MODE, lc, 0);
        gpio_ll_func_sel(s_gpio, (uint8_t)pin, PIN_FUNC_GPIO);
        gpio_ll_input_enable(s_gpio, pin);
        gpio_ll_output_enable(s_gpio, pin);
        esp_rom_gpio_connect_out_signal(pin, LEDC_LS_SIG_OUT0_IDX + ch, false, false);
        s_ch_pin[ch] = (int8_t)pin;
        s_pwm_ch[pin] = (int8_t)(ch + 1);
    }
    ledc_channel_t lc = (ledc_channel_t)ch;
    uint32_t level = (uint32_t)(duty * 1000.0f);   // same 1/1000 steps as the Pico
    uint32_t val = (level * ((1u << PWM_BITS) - 1) + 500) / 1000;
    ledc_ll_set_duty_int_part(hw, LEDC_LOW_SPEED_MODE, lc, val);
    ledc_ll_set_duty_direction(hw, LEDC_LOW_SPEED_MODE, lc, LEDC_DUTY_DIR_INCREASE);
    ledc_ll_set_duty_num(hw, LEDC_LOW_SPEED_MODE, lc, 1);
    ledc_ll_set_duty_cycle(hw, LEDC_LOW_SPEED_MODE, lc, 1);
    ledc_ll_set_duty_scale(hw, LEDC_LOW_SPEED_MODE, lc, 0);
    ledc_ll_set_sig_out_en(hw, LEDC_LOW_SPEED_MODE, lc, true);
    ledc_ll_set_duty_start(hw, LEDC_LOW_SPEED_MODE, lc);
    ledc_ll_ls_channel_update(hw, LEDC_LOW_SPEED_MODE, lc);
    return HAL_OK;
}

int hal_pwm_stop(int pin)
{
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    if (s_pwm_ch[pin]) pwm_release(pin);
    return hal_gpio_mode(pin, HAL_GPIO_OUTPUT);   // output driven low
}

// ---------- I2C: polled master on I2C0/I2C1 ----------
// A pin pair keeps its controller; a new pair takes a free one, or
// re-initialises controller 0 when both are in use (same rule as the Arduino
// backend). The initialisation and polling follow the course port's validated
// driver (device/i2c/sysdepend/esp32s3/i2c_esp32s3.c): poll the raw interrupt
// status, because the masked one stays zero with interrupts disabled, and run
// a register read as one command list. A write with nostop is therefore not
// sent at once: it is held and issued together with the next read as
//   RESTART, WRITE(addr|W, data), RESTART, WRITE(addr|R), READ.., STOP
// which is also what Arduino's Wire does with endTransmission(false). A NACK
// in either phase makes that read fail with HAL_ERR_IO. Transfers are limited
// by the 32-byte FIFOs: 31 data bytes per write, 32 per read (the shared
// driver uses at most 4 and 6).

#define I2C_FIFO          32
#define I2C_SCL_TMO_US    20000          // IDF's value; the port matches it
#define I2C_POLL_US       25000

typedef struct {
    int     sda, scl;
    bool    pending;                     // a nostop write waiting for its read
    uint8_t pend_addr;
    uint8_t pend_len;
    uint8_t pend[I2C_FIFO];
} i2c_bus_t;

static i2c_bus_t s_i2c[2] = { { .sda = -1, .scl = -1 }, { .sda = -1, .scl = -1 } };

static void i2c_pin(int pin, uint32_t out_sig, uint32_t in_sig)
{
    gpio_ll_func_sel(s_gpio, (uint8_t)pin, PIN_FUNC_GPIO);
    gpio_ll_set_level(s_gpio, pin, 1);
    gpio_ll_input_enable(s_gpio, pin);
    gpio_ll_output_enable(s_gpio, pin);
    gpio_ll_od_enable(s_gpio, pin);
    gpio_ll_pulldown_dis(s_gpio, pin);
    gpio_ll_pullup_en(s_gpio, pin);
    esp_rom_gpio_connect_out_signal(pin, out_sig, false, false);
    esp_rom_gpio_connect_in_signal(pin, in_sig, false);
}

static void i2c_recover(i2c_dev_t *hw)
{
    i2c_ll_master_fsm_rst(hw);
    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);
    i2c_ll_clear_intr_mask(hw, UINT32_MAX);
}

int hal_i2c_open(int sda, int scl, uint32_t baud)
{
    if (!hal_gpio_valid(sda) || !hal_gpio_valid(scl) || sda == scl) return HAL_ERR_PIN;
    int bus = -1;
    for (int b = 0; b < 2 && bus < 0; b++)
        if (s_i2c[b].sda == sda && s_i2c[b].scl == scl) bus = b;
    for (int b = 0; b < 2 && bus < 0; b++)
        if (s_i2c[b].sda < 0) bus = b;
    if (bus < 0) bus = 0;
    if (baud == 0) baud = 100000;

    i2c_dev_t *hw = I2C_LL_GET_HW(bus);
    PERIPH_RCC_ATOMIC() {
        i2c_ll_enable_bus_clock(bus, true);
        i2c_ll_reset_register(bus);
    }
    i2c_ll_enable_controller_clock(hw, true);
    i2c_ll_set_source_clk(hw, (i2c_clock_source_t)I2C_CLK_SRC_DEFAULT);
    i2c_hal_clk_config_t clk = { 0 };
    i2c_ll_master_cal_bus_clk(XTAL_HZ, baud, &clk);
    i2c_ll_master_set_bus_timing(hw, &clk);
    i2c_ll_set_mode(hw, I2C_BUS_MODE_MASTER);
    i2c_ll_enable_pins_open_drain(hw, true);
    i2c_ll_enable_arbitration(hw, false);
    i2c_ll_master_rx_full_ack_level(hw, false);
    i2c_ll_set_data_mode(hw, I2C_DATA_MODE_MSB_FIRST, I2C_DATA_MODE_MSB_FIRST);
    i2c_ll_master_set_filter(hw, 7);
    i2c_ll_enable_fifo_mode(hw, true);
    i2c_ll_set_tout(hw, i2c_ll_calculate_timeout_us_to_reg_val(XTAL_HZ, I2C_SCL_TMO_US));
    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);
    i2c_ll_disable_intr_mask(hw, UINT32_MAX);
    i2c_ll_clear_intr_mask(hw, UINT32_MAX);
    i2c_pin(sda, bus ? I2CEXT1_SDA_OUT_IDX : I2CEXT0_SDA_OUT_IDX, bus ? I2CEXT1_SDA_IN_IDX : I2CEXT0_SDA_IN_IDX);
    i2c_pin(scl, bus ? I2CEXT1_SCL_OUT_IDX : I2CEXT0_SCL_OUT_IDX, bus ? I2CEXT1_SCL_IN_IDX : I2CEXT0_SCL_IN_IDX);
    i2c_ll_update(hw);
    s_i2c[bus].sda = sda;
    s_i2c[bus].scl = scl;
    s_i2c[bus].pending = false;
    return bus;
}

static void i2c_cmd(i2c_dev_t *hw, int *idx, int op, int bytes, bool ack_check, int ack_val)
{
    i2c_ll_hw_cmd_t c = { 0 };
    c.op_code = op;
    c.byte_num = bytes;
    c.ack_en = ack_check;
    c.ack_val = ack_val;
    i2c_ll_master_write_cmd_reg(hw, c, (*idx)++);
}

// One transfer as a single command list ending with STOP: optional write of
// wlen bytes, then optional read of rlen bytes after a repeated START.
static int i2c_transfer(int bus, uint8_t addr, const uint8_t *w, size_t wlen, bool do_write,
                        uint8_t *r, size_t rlen)
{
    i2c_dev_t *hw = I2C_LL_GET_HW(bus);
    int idx = 0;
    i2c_recover(hw);
    if (do_write) {
        uint8_t a = (uint8_t)(addr << 1);
        i2c_ll_write_txfifo(hw, &a, 1);
        if (wlen) i2c_ll_write_txfifo(hw, w, (uint8_t)wlen);
        i2c_cmd(hw, &idx, I2C_LL_CMD_RESTART, 0, false, 0);
        i2c_cmd(hw, &idx, I2C_LL_CMD_WRITE, (int)wlen + 1, true, 0);
    }
    if (rlen) {
        uint8_t a = (uint8_t)((addr << 1) | 1);
        i2c_ll_write_txfifo(hw, &a, 1);
        i2c_cmd(hw, &idx, I2C_LL_CMD_RESTART, 0, false, 0);
        i2c_cmd(hw, &idx, I2C_LL_CMD_WRITE, 1, true, 0);
        if (rlen > 1) i2c_cmd(hw, &idx, I2C_LL_CMD_READ, (int)rlen - 1, false, 0);
        i2c_cmd(hw, &idx, I2C_LL_CMD_READ, 1, false, 1);      // NACK the last byte
    }
    i2c_cmd(hw, &idx, I2C_LL_CMD_STOP, 0, false, 0);

    i2c_ll_update(hw);
    i2c_ll_start_trans(hw);
    uint64_t t0 = esp_timer_get_time();
    uint32_t raw = 0;
    const uint32_t fail = I2C_LL_INTR_NACK | I2C_LL_INTR_ARBITRATION | I2C_LL_INTR_TIMEOUT;
    while (!((raw = hw->int_raw.val) & (fail | I2C_LL_INTR_MST_COMPLETE))) {
        if (esp_timer_get_time() - t0 > I2C_POLL_US) break;
    }
    if (!(raw & I2C_LL_INTR_MST_COMPLETE) || (raw & fail)) {
        i2c_recover(hw);
        return HAL_ERR_IO;
    }
    if (rlen) i2c_ll_read_rxfifo(hw, r, (uint8_t)rlen);
    i2c_ll_clear_intr_mask(hw, UINT32_MAX);
    return HAL_OK;
}

int hal_i2c_write(int bus, uint8_t addr, const uint8_t *data, size_t len, bool nostop)
{
    if (bus < 0 || bus > 1 || s_i2c[bus].sda < 0) return HAL_ERR_INVALID;
    if (len > I2C_FIFO - 1) return HAL_ERR_INVALID;
    i2c_bus_t *b = &s_i2c[bus];
    if (b->pending) {                    // an earlier nostop write never got its read
        b->pending = false;
        (void)i2c_transfer(bus, b->pend_addr, b->pend, b->pend_len, true, NULL, 0);
    }
    if (nostop) {
        b->pending = true;
        b->pend_addr = addr;
        b->pend_len = (uint8_t)len;
        if (len) memcpy(b->pend, data, len);
        return (int)len;
    }
    int r = i2c_transfer(bus, addr, data, len, true, NULL, 0);
    return r == HAL_OK ? (int)len : r;
}

int hal_i2c_read(int bus, uint8_t addr, uint8_t *buf, size_t len)
{
    if (bus < 0 || bus > 1 || s_i2c[bus].sda < 0) return HAL_ERR_INVALID;
    if (len == 0 || len > I2C_FIFO) return HAL_ERR_INVALID;
    i2c_bus_t *b = &s_i2c[bus];
    int r;
    if (b->pending && b->pend_addr == addr) {
        b->pending = false;
        r = i2c_transfer(bus, addr, b->pend, b->pend_len, true, buf, len);
    } else {
        if (b->pending) {
            b->pending = false;
            (void)i2c_transfer(bus, b->pend_addr, b->pend, b->pend_len, true, NULL, 0);
        }
        r = i2c_transfer(bus, addr, NULL, 0, false, buf, len);
    }
    return r == HAL_OK ? (int)len : r;
}

// ---------- UART: UART2, then UART1, interrupt-fed receive rings ----------
// UART0 stays free. The course port's generic board registers a sample UART1
// device ("sera", GPIO43/44) at boot; this backend takes UART1 over only when
// a second UART is opened, so that device must not be used alongside it.

#define UART_RING 512                       // power of two

typedef struct {
    int     num;                            // 2 or 1
    int     tx, rx;
    uint8_t ring[UART_RING];
    volatile uint32_t head, tail;           // head: handler only; tail: reader only
    volatile uint32_t dropped;
    bool    irq;
} uart_port_ctx_t;

static uart_port_ctx_t s_uart[2] = { { .num = 2, .tx = -1, .rx = -1 }, { .num = 1, .tx = -1, .rx = -1 } };

static void uart_isr(UINT intno)
{
    uart_port_ctx_t *u = intno == ETS_UART2_INTR_SOURCE ? &s_uart[0] : &s_uart[1];
    uart_dev_t *hw = UART_LL_GET_HW(u->num);
    uint32_t st = uart_ll_get_intsts_mask(hw);
    uint32_t n;
    while ((n = uart_ll_get_rxfifo_len(hw)) > 0) {
        uint8_t b;
        uart_ll_read_rxfifo(hw, &b, 1);
        uint32_t head = u->head;
        if (head - __atomic_load_n(&u->tail, __ATOMIC_ACQUIRE) >= UART_RING) { u->dropped++; continue; }
        u->ring[head & (UART_RING - 1)] = b;
        __atomic_store_n(&u->head, head + 1, __ATOMIC_RELEASE);
    }
    uart_ll_clr_intsts_mask(hw, st);
}

int hal_uart_open(int tx, int rx, uint32_t baud, uint8_t bits, char parity, uint8_t stop)
{
    if (!hal_gpio_valid(tx) || !hal_gpio_valid(rx) || tx == rx) return HAL_ERR_PIN;
    int port = -1;
    for (int p = 0; p < 2 && port < 0; p++)
        if (s_uart[p].tx == tx && s_uart[p].rx == rx) port = p;
    for (int p = 0; p < 2 && port < 0; p++)
        if (s_uart[p].tx < 0) port = p;
    if (port < 0) port = 0;
    uart_port_ctx_t *u = &s_uart[port];
    uart_dev_t *hw = UART_LL_GET_HW(u->num);
    UINT intno = u->num == 2 ? ETS_UART2_INTR_SOURCE : ETS_UART1_INTR_SOURCE;

    if (u->irq) DisableInt(intno);
    PERIPH_RCC_ATOMIC() {
        uart_ll_enable_bus_clock((uart_port_t)u->num, true);
        uart_ll_reset_register((uart_port_t)u->num);
    }
    uart_ll_sclk_enable(hw);
    uart_ll_set_sclk(hw, (soc_module_clk_t)UART_SCLK_XTAL);
    uart_ll_set_mode_normal(hw);
    uart_ll_set_baudrate(hw, baud ? baud : 115200, XTAL_HZ);
    uart_ll_set_data_bit_num(hw, bits == 5 ? UART_DATA_5_BITS : bits == 6 ? UART_DATA_6_BITS
                                 : bits == 7 ? UART_DATA_7_BITS : UART_DATA_8_BITS);
    uart_ll_set_parity(hw, (parity == 'E' || parity == 'e') ? UART_PARITY_EVEN
                         : (parity == 'O' || parity == 'o') ? UART_PARITY_ODD : UART_PARITY_DISABLE);
    uart_ll_set_stop_bits(hw, stop == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1);
    uart_ll_set_hw_flow_ctrl(hw, UART_HW_FLOWCTRL_DISABLE, 0);
    uart_ll_set_loop_back(hw, false);
    uart_ll_set_tx_idle_num(hw, 0);
    uart_ll_rxfifo_rst(hw);
    uart_ll_txfifo_rst(hw);
    uart_ll_set_rxfifo_full_thr(hw, 16);
    uart_ll_set_rx_tout(hw, 10);             // receive timeout after about one idle frame
    uart_ll_disable_intr_mask(hw, UART_LL_INTR_MASK);
    uart_ll_clr_intsts_mask(hw, UART_LL_INTR_MASK);

    gpio_ll_func_sel(s_gpio, (uint8_t)tx, PIN_FUNC_GPIO);
    gpio_ll_set_level(s_gpio, tx, 1);
    gpio_ll_output_enable(s_gpio, tx);
    esp_rom_gpio_connect_out_signal(tx, u->num == 2 ? U2TXD_OUT_IDX : U1TXD_OUT_IDX, false, false);
    gpio_ll_func_sel(s_gpio, (uint8_t)rx, PIN_FUNC_GPIO);
    gpio_ll_output_disable(s_gpio, rx);
    gpio_ll_input_enable(s_gpio, rx);
    gpio_ll_pullup_en(s_gpio, rx);
    esp_rom_gpio_connect_in_signal(rx, u->num == 2 ? U2RXD_IN_IDX : U1RXD_IN_IDX, false);

    // Let the line settle for about two characters after the pins move, then
    // discard anything the reconfiguration produced, so the first byte read
    // is a real one.
    uint32_t settle = 20000000u / (baud ? baud : 115200) + 50;
    esp_rom_delay_us(settle > 3000 ? 3000 : settle);
    uart_ll_rxfifo_rst(hw);
    uart_ll_clr_intsts_mask(hw, UART_LL_INTR_MASK);
    u->head = u->tail = 0;
    u->tx = tx;
    u->rx = rx;
    if (!u->irq) {
        T_DINT dint = { .intatr = TA_HLNG, .inthdr = (FP)uart_isr };
        if (tk_def_int(intno, &dint) != E_OK) return HAL_ERR_UNSUPPORTED;
        u->irq = true;
    }
    uart_ll_ena_intr_mask(hw, UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_OVF);
    EnableInt(intno, 1);
    return port;
}

int hal_uart_write(int port, const uint8_t *data, size_t len)
{
    uart_port_ctx_t *u = &s_uart[port & 1];
    uart_dev_t *hw = UART_LL_GET_HW(u->num);
    size_t done = 0;
    while (done < len) {
        uint32_t room = uart_ll_get_txfifo_len(hw);
        if (!room) {
            if (tk_dly_tsk(1) != E_OK) esp_rom_delay_us(100);
            continue;
        }
        uint32_t n = (uint32_t)(len - done) < room ? (uint32_t)(len - done) : room;
        uart_ll_write_txfifo(hw, data + done, n);
        done += n;
    }
    return (int)len;
}

int hal_uart_read_byte(int port)
{
    uart_port_ctx_t *u = &s_uart[port & 1];
    uint32_t tail = u->tail;
    if (__atomic_load_n(&u->head, __ATOMIC_ACQUIRE) == tail) return HAL_ERR_TIMEOUT;
    int ch = u->ring[tail & (UART_RING - 1)];
    __atomic_store_n(&u->tail, tail + 1, __ATOMIC_RELEASE);
    return ch;
}

void hal_mtk_uart_internal_loopback(int port, bool on)
{
    uart_ll_set_loop_back(UART_LL_GET_HW(s_uart[port & 1].num), on);
}

uint32_t hal_mtk_uart_dropped(int port) { return s_uart[port & 1].dropped; }

// ---------- SPI ----------
// SPI2/FSPI, polled 64-byte FIFO chunks. CS is kept low across the entire
// transaction, including 1024-byte benchmark transfers. No DMA or IDF driver
// task/FreeRTOS object is involved. All device/host state is mutex protected.

#define SPI_DEVICES 8
#define SPI_FIFO_BYTES 64
#define SPI_MAX_BYTES 1024

typedef struct {
    bool active;
    int cs;
    uint32_t hz;
    uint8_t mode;
    uint8_t refs;
} spi_device_t;

static ID s_spi_mtx;
static spi_device_t s_spi_devices[SPI_DEVICES];
static int s_spi_sclk = -1, s_spi_mosi = -1, s_spi_miso = -1;
static unsigned s_spi_refs;

static int spi_lock(uint32_t timeout_ms)
{
    if (s_spi_mtx <= 0) return HAL_ERR_NO_MEMORY;
    ER er = tk_loc_mtx(s_spi_mtx, timeout_ms ? (TMO)timeout_ms : TMO_POL);
    return er == E_OK ? HAL_OK : (er == E_TMOUT ? HAL_ERR_TIMEOUT : HAL_ERR_IO);
}

static void spi_pin_out(int pin, uint32_t signal)
{
    gpio_ll_func_sel(s_gpio, (uint8_t)pin, PIN_FUNC_GPIO);
    gpio_ll_od_disable(s_gpio, pin);
    gpio_ll_input_enable(s_gpio, pin);
    gpio_ll_output_enable(s_gpio, pin);
    gpio_ll_pulldown_dis(s_gpio, pin);
    gpio_ll_pullup_dis(s_gpio, pin);
    esp_rom_gpio_connect_out_signal(pin, signal, false, false);
}

static void spi_pin_in(int pin, uint32_t signal)
{
    gpio_ll_func_sel(s_gpio, (uint8_t)pin, PIN_FUNC_GPIO);
    gpio_ll_output_disable(s_gpio, pin);
    gpio_ll_input_enable(s_gpio, pin);
    gpio_ll_pulldown_dis(s_gpio, pin);
    gpio_ll_pullup_dis(s_gpio, pin);
    esp_rom_gpio_connect_in_signal(pin, signal, false);
}

int hal_spi_open(const char *host, int sclk, int mosi, int miso, int cs,
                 uint32_t hz, uint8_t mode)
{
    if (!host || strcmp(host, "fspi") != 0 || mode > 3 || hz < 1000 || hz > 40000000)
        return HAL_ERR_INVALID;
    if (!hal_gpio_valid(sclk) || !hal_gpio_valid(mosi) || !hal_gpio_valid(cs) ||
        (miso >= 0 && !hal_gpio_valid(miso)) || sclk == mosi || sclk == cs ||
        mosi == cs || (miso >= 0 && (miso == sclk || miso == mosi || miso == cs)))
        return HAL_ERR_PIN;
    int er = spi_lock(1000);
    if (er < 0) return er;
    if (s_spi_refs && (s_spi_sclk != sclk || s_spi_mosi != mosi || s_spi_miso != miso)) {
        tk_unl_mtx(s_spi_mtx);
        return HAL_ERR_BUSY;
    }
    for (int i = 0; i < SPI_DEVICES; ++i) {
        spi_device_t *d = &s_spi_devices[i];
        if (!d->active || d->cs != cs) continue;
        if (d->hz != hz || d->mode != mode || d->refs == UINT8_MAX) {
            tk_unl_mtx(s_spi_mtx);
            return HAL_ERR_BUSY;
        }
        ++d->refs;
        ++s_spi_refs;
        tk_unl_mtx(s_spi_mtx);
        return i;
    }
    int slot = -1;
    for (int i = 0; i < SPI_DEVICES; ++i)
        if (!s_spi_devices[i].active) { slot = i; break; }
    if (slot < 0) {
        tk_unl_mtx(s_spi_mtx);
        return HAL_ERR_BUSY;
    }
    if (!s_spi_refs) {
        PERIPH_RCC_ATOMIC() {
            spi_ll_enable_bus_clock(SPI2_HOST, true);
            spi_ll_reset_register(SPI2_HOST);
        }
        spi_ll_enable_clock(SPI2_HOST, true);
        spi_dev_t *hw = SPI_LL_GET_HW(1); // SPI2_HOST is register index 1
        spi_ll_set_clk_source(hw, SPI_CLK_SRC_DEFAULT); // APB, 80 MHz
        spi_ll_master_init(hw);
        spi_ll_enable_int(hw);
        spi_ll_set_int_stat(hw);
        spi_ll_clear_int_stat(hw);
        spi_ll_set_mosi_delay(hw, 0, 0);
        spi_ll_set_half_duplex(hw, false);
        spi_ll_set_tx_lsbfirst(hw, false);
        spi_ll_set_rx_lsbfirst(hw, false);
        spi_ll_set_dummy(hw, 0);
        spi_ll_set_command_bitlen(hw, 0);
        spi_ll_set_addr_bitlen(hw, 0);
        spi_line_mode_t line = { .cmd_lines = 1, .addr_lines = 1, .data_lines = 1 };
        spi_ll_master_set_line_mode(hw, line);
        spi_ll_enable_mosi(hw, true);
        spi_ll_enable_miso(hw, miso >= 0);
        spi_pin_out(sclk, FSPICLK_OUT_IDX);
        spi_pin_out(mosi, FSPID_OUT_IDX);
        if (miso >= 0) spi_pin_in(miso, FSPIQ_IN_IDX);
        s_spi_sclk = sclk;
        s_spi_mosi = mosi;
        s_spi_miso = miso;
    }
    gpio_ll_func_sel(s_gpio, (uint8_t)cs, PIN_FUNC_GPIO);
    gpio_ll_od_disable(s_gpio, cs);
    esp_rom_gpio_connect_out_signal(cs, SIG_GPIO_OUT_IDX, false, false);
    gpio_ll_set_level(s_gpio, cs, 1);
    gpio_ll_output_enable(s_gpio, cs);
    s_spi_devices[slot] = (spi_device_t){ .active = true, .cs = cs, .hz = hz, .mode = mode, .refs = 1 };
    ++s_spi_refs;
    tk_unl_mtx(s_spi_mtx);
    return slot;
}

int hal_spi_transfer(int device, const uint8_t *tx, uint8_t *rx, size_t len, uint32_t timeout_ms)
{
    if (device < 0 || device >= SPI_DEVICES || len == 0 || len > SPI_MAX_BYTES || timeout_ms == 0)
        return HAL_ERR_INVALID;
    uint64_t deadline = hal_time_us() + (uint64_t)timeout_ms * 1000;
    int er = spi_lock(timeout_ms);
    if (er < 0) return er;
    spi_device_t d = s_spi_devices[device];
    if (!d.active || (rx && s_spi_miso < 0)) {
        tk_unl_mtx(s_spi_mtx);
        return HAL_ERR_INVALID;
    }
    spi_dev_t *hw = SPI_LL_GET_HW(1);
    spi_ll_master_set_mode(hw, d.mode);
    spi_ll_master_set_clock(hw, APB_HZ, (int)d.hz, 128);
    gpio_ll_set_level(s_gpio, d.cs, 0);
    int result = (int)len;
    for (size_t off = 0; off < len; off += SPI_FIFO_BYTES) {
        size_t n = len - off;
        if (n > SPI_FIFO_BYTES) n = SPI_FIFO_BYTES;
        uint8_t data[SPI_FIFO_BYTES] = { 0 };
        if (tx) memcpy(data, tx + off, n);
        spi_ll_set_mosi_bitlen(hw, n * 8);
        spi_ll_set_miso_bitlen(hw, n * 8);
        spi_ll_write_buffer_byte(hw, 0, data, (int)n);
        hw->cmd.update = 1;
        while (hw->cmd.update && hal_time_us() < deadline) { }
        if (hw->cmd.update) { result = HAL_ERR_TIMEOUT; break; }
        // The completion flag is sticky. Never accept a previous chunk's flag
        // (or the init-time set) as completion of this transfer.
        spi_ll_clear_int_stat(hw);
        spi_ll_user_start(hw);
        while (!spi_ll_usr_is_done(hw) && hal_time_us() < deadline) { }
        if (!spi_ll_usr_is_done(hw)) { result = HAL_ERR_TIMEOUT; break; }
        if (rx) spi_ll_read_buffer_byte(hw, 0, rx + off, (int)n);
        spi_ll_clear_int_stat(hw);
    }
    gpio_ll_set_level(s_gpio, d.cs, 1);
    if (hal_time_us() >= deadline) result = HAL_ERR_TIMEOUT;
    tk_unl_mtx(s_spi_mtx);
    return result;
}

int hal_spi_close(int device)
{
    if (device < 0 || device >= SPI_DEVICES) return HAL_ERR_INVALID;
    int er = spi_lock(1000);
    if (er < 0) return er;
    spi_device_t *d = &s_spi_devices[device];
    if (!d->active || !d->refs) {
        tk_unl_mtx(s_spi_mtx);
        return HAL_ERR_INVALID;
    }
    --d->refs;
    --s_spi_refs;
    if (!d->refs) {
        gpio_ll_set_level(s_gpio, d->cs, 1);
        memset(d, 0, sizeof(*d));
    }
    if (!s_spi_refs) {
        spi_ll_enable_clock(SPI2_HOST, false);
        PERIPH_RCC_ATOMIC() { spi_ll_enable_bus_clock(SPI2_HOST, false); }
        s_spi_sclk = s_spi_mosi = s_spi_miso = -1;
    }
    tk_unl_mtx(s_spi_mtx);
    return HAL_OK;
}

// ---------- start-up ----------

ER hal_mtk_init(void)
{
    T_CMTX spi_cmtx = { .exinf = NULL, .mtxatr = TA_TFIFO | TA_INHERIT, .ceilpri = 0 };
    s_spi_mtx = tk_cre_mtx(&spi_cmtx);
    if (s_spi_mtx < E_OK) return (ER)s_spi_mtx;
    for (int p = 0; p < NPINS; p++) s_pwm_ch[p] = 0;
    for (int i = 0; i < PULSE_SLOTS; i++) {
        s_pulse[i].pin = -1;
        T_CALM calm = { .exinf = &s_pulse[i], .almatr = TA_HLNG, .almhdr = (FP)pulse_end };
        ID id = tk_cre_alm(&calm);
        if (id < E_OK) return (ER)id;
        s_pulse[i].alm = id;
    }
    T_CFLG cflg = { .exinf = NULL, .flgatr = TA_TFIFO | TA_WSGL, .iflgptn = 0 };
    ID flg = tk_cre_flg(&cflg);
    if (flg < E_OK) return (ER)flg;
    s_echo_flg = flg;
    // GPIO interrupt on the calling core (SchedulerTask: core 1).
    s_gpio_core = (uint32_t)esp_cpu_get_core_id();
    T_DINT dint = { .intatr = TA_HLNG, .inthdr = (FP)gpio_isr };
    ER er = tk_def_int(ETS_GPIO_INTR_SOURCE, &dint);
    if (er != E_OK) return er;
    EnableInt(ETS_GPIO_INTR_SOURCE, 1);
    s_gpio_irq = true;
    return E_OK;
}
