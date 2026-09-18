#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"

#include "bus/bus_common.h"
#include "bus/bus_if.h"

/* ----------------------------------------------------------------------------
 * Context per GPIO sensor
 * --------------------------------------------------------------------------*/
typedef struct {
    app_cfg_t      g;           // full parsed config snapshot for this sensor
    absolute_time_t last_change;
    absolute_time_t raw_change;
    uint8_t        last_state;  // debounced state (for digital/counter)
    uint8_t        last_raw;    // last raw GPIO value
    uint32_t       counter;     // edge counter (for "counter" mode)
    bool           inited;
} gpio_ctx_t;


/* ----------------------------------------------------------------------------
 * Small helpers
 * --------------------------------------------------------------------------*/
static inline uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static inline uint8_t read_input(uint pin, bool invert) {
    uint8_t v = (uint8_t)gpio_get(pin);
    return invert ? (uint8_t)(!v) : v;
}

/* Debounce only after the raw input has held its candidate state for db ms. */
static uint8_t debounce_read(gpio_ctx_t *c) {
    const uint32_t db = (c->g.gpio_debounce_ms > 0) ? (uint32_t)c->g.gpio_debounce_ms : 0;
    const uint8_t  cur = read_input((uint)c->g.gpio_pin, c->g.gpio_invert != 0);

    if (db == 0) {
        // no debouncing requested
        if (cur != c->last_state) {
            c->last_state  = cur;
            c->last_change = get_absolute_time();
        }
        return c->last_state;
    }

    if (cur != c->last_raw) {
        c->last_raw = cur;
        c->raw_change = get_absolute_time();
    }

    if (c->last_raw != c->last_state) {
        const uint32_t elapsed = (uint32_t)(
            to_ms_since_boot(get_absolute_time()) -
            to_ms_since_boot(c->raw_change)
        );
        if (elapsed >= db) {
            c->last_state  = c->last_raw;
            c->last_change = get_absolute_time();
        }
    }
    return c->last_state;
}
// Single source of truth: gpio_mode string
static const char *get_gpio_method(const gpio_ctx_t *c) {
    if (c->g.gpio_mode && *c->g.gpio_mode)
        return c->g.gpio_mode;
    return "digital";
}


static inline bool mode_is_digital(const gpio_ctx_t *c) {
    const char *m = get_gpio_method(c);
    return (strcmp(m, "digital") == 0);
}

static inline bool mode_is_pulse(const gpio_ctx_t *c) {
    const char *m = get_gpio_method(c);
    return (strcmp(m, "pulse") == 0);
}

static inline bool mode_is_onewire(const gpio_ctx_t *c) {
    const char *m = get_gpio_method(c);
    return (strcmp(m, "onewire") == 0);
}

static inline bool mode_is_counter(const gpio_ctx_t *c) {
    const char *m = get_gpio_method(c);
    return (strcmp(m, "counter") == 0);
}


/* Pull mode helper: "up" / "down" / "off" (default off if unknown) */
static void apply_pull(uint pin, const char *pull_str) {
    if (!pull_str || strcmp(pull_str, "off") == 0) {
        gpio_disable_pulls(pin);
    } else if (strcmp(pull_str, "up") == 0) {
        gpio_pull_up(pin);
    } else if (strcmp(pull_str, "down") == 0) {
        gpio_pull_down(pin);
    } else {
        // default: no pull
        gpio_disable_pulls(pin);
    }
}

/* Measure a pulse on echo_pin after driving trig_pin */
static int measure_pulse_us(uint trig_pin, uint echo_pin,
                            uint32_t trig_us, uint32_t timeout_us)
{
    if (trig_us == 0)        trig_us        = 10;
    if (timeout_us == 0)     timeout_us     = 30000;

    // ensure both are initialised
    gpio_init(trig_pin);
    gpio_set_dir(trig_pin, GPIO_OUT);
    gpio_put(trig_pin, 0);

    gpio_init(echo_pin);
    gpio_set_dir(echo_pin, GPIO_IN);
    gpio_disable_pulls(echo_pin);

    sleep_us(2);

    // send trigger pulse
    gpio_put(trig_pin, 1);
    sleep_us(trig_us);
    gpio_put(trig_pin, 0);

    // wait for echo HIGH
    absolute_time_t t0 = get_absolute_time();
    while (!gpio_get(echo_pin)) {
        if (absolute_time_diff_us(t0, get_absolute_time()) > (int64_t)timeout_us)
            return -1;
    }

    // echo HIGH -> measure width
    absolute_time_t start = get_absolute_time();
    while (gpio_get(echo_pin)) {
        if (absolute_time_diff_us(start, get_absolute_time()) > (int64_t)timeout_us)
            break;
    }
    absolute_time_t end = get_absolute_time();
    int32_t width = (int32_t)absolute_time_diff_us(start, end);
    if (width < 0) width = 0;
    return (int)width; // microseconds
}

/* ----------------------------------------------------------------------------
 * Generic DHT-style one-wire read (AM2302 / DHT22 framing)
 *   - pin: data pin with pull-up
 *   - humidity_x10: 0.1 %RH
 *   - temp_x10: 0.1 °C (signed)
 * Returns 0 on success, non-zero on error.
 * --------------------------------------------------------------------------*/
static int onewire_dht_read(uint pin, uint16_t *humidity_x10, int16_t *temp_x10)
{
    if (!humidity_x10 || !temp_x10) return -1;

    // Drive start signal
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
    sleep_ms(2);         // >1 ms start
    gpio_put(pin, 1);
    sleep_us(30);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);

    // Wait for sensor response: ~80us low, then 80us high
    absolute_time_t t0 = get_absolute_time();
    while (gpio_get(pin)) {
        if (absolute_time_diff_us(t0, get_absolute_time()) > 200u)
            return -2; // no response
    }
    while (!gpio_get(pin)) {
        if (absolute_time_diff_us(t0, get_absolute_time()) > 400u)
            return -3; // stuck low
    }
    while (gpio_get(pin)) {
        if (absolute_time_diff_us(t0, get_absolute_time()) > 600u)
            return -4; // stuck high
    }

    // Now 40 bits: 50 us low + (26..28 us high => 0, ~70 us high => 1)
    uint8_t data[5] = {0,0,0,0,0};

    for (int i = 0; i < 40; i++) {
        // wait for 50us low period
        absolute_time_t t_low = get_absolute_time();
        while (!gpio_get(pin)) {
            if (absolute_time_diff_us(t_low, get_absolute_time()) > 200u)
                return -5;
        }

        // measure high duration
        absolute_time_t t_high = get_absolute_time();
        while (gpio_get(pin)) {
            if (absolute_time_diff_us(t_high, get_absolute_time()) > 200u)
                break;
        }
        int high_us = (int)absolute_time_diff_us(t_high, get_absolute_time());

        int bit = (high_us > 50) ? 1 : 0; // threshold ~50us
        data[i / 8] <<= 1;
        data[i / 8] |= (uint8_t)bit;
    }

    // checksum
    uint8_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (sum != data[4]) {
        return -6;
    }

    uint16_t hum = ((uint16_t)data[0] << 8) | data[1];
    uint16_t t   = ((uint16_t)data[2] << 8) | data[3];

    int16_t temp10;
    if (t & 0x8000) {
        t      &= 0x7FFF;
        temp10  = -(int16_t)t;
    } else {
        temp10  = (int16_t)t;
    }

    *humidity_x10 = hum;
    *temp_x10     = temp10;
    return 0;
}

/* ----------------------------------------------------------------------------
 * bus_if: init_cfg
 * --------------------------------------------------------------------------*/
static int gpio_init_cfg(const sensor_cfg_t *scfg, void **ctx_out)
{
    if (!ctx_out || !scfg) return -1;

    // Get the real parsed config (same as I2C does)
    const app_cfg_t *cfg = (const app_cfg_t *)scfg->kv_pairs;

    gpio_ctx_t *ctx = (gpio_ctx_t *)calloc(1, sizeof(gpio_ctx_t));
    if (!ctx) return -2;

    // Take a snapshot of the config for this sensor
    memset(&ctx->g, 0, sizeof(ctx->g));
    if (cfg) {
        memcpy(&ctx->g, cfg, sizeof(app_cfg_t));
    }

    // --------- Fill sane defaults for any zero/NULL fields ----------
    if (!ctx->g.gpio_mode)              ctx->g.gpio_mode = "digital";
    if (!ctx->g.gpio_pull)              ctx->g.gpio_pull = "off";
    if (ctx->g.gpio_pin == 0)           ctx->g.gpio_pin  = 2;
    if (ctx->g.gpio_debounce_ms <= 0)   ctx->g.gpio_debounce_ms = 50;

    // Pulse defaults
    if (ctx->g.gpio_trig_us <= 0)          ctx->g.gpio_trig_us          = 10;
    if (ctx->g.gpio_pulse_timeout_us <= 0) ctx->g.gpio_pulse_timeout_us = 30000;

    // --------- GPIO init according to mode ----------
    if (mode_is_digital(ctx) || mode_is_counter(ctx)) {
        gpio_init((uint)ctx->g.gpio_pin);
        gpio_set_dir((uint)ctx->g.gpio_pin, GPIO_IN);
        apply_pull((uint)ctx->g.gpio_pin, ctx->g.gpio_pull);

        uint8_t raw = (uint8_t)gpio_get((uint)ctx->g.gpio_pin);
        if (ctx->g.gpio_invert) raw = !raw;

        ctx->last_raw    = raw;
        ctx->last_state  = raw;
        ctx->last_change = get_absolute_time();
        ctx->raw_change  = ctx->last_change;
        ctx->counter     = 0;
        ctx->inited      = true;

    } else if (mode_is_pulse(ctx)) {
        gpio_init((uint)ctx->g.gpio_trig);
        gpio_set_dir((uint)ctx->g.gpio_trig, GPIO_OUT);
        gpio_put((uint)ctx->g.gpio_trig, 0);

        gpio_init((uint)ctx->g.gpio_echo);
        gpio_set_dir((uint)ctx->g.gpio_echo, GPIO_IN);
        gpio_disable_pulls((uint)ctx->g.gpio_echo);

        ctx->inited = true;

    } else if (mode_is_onewire(ctx)) {
        // Generic 1-wire: just set input with pull-up.
        gpio_init((uint)ctx->g.gpio_pin);
        gpio_set_dir((uint)ctx->g.gpio_pin, GPIO_IN);
        apply_pull((uint)ctx->g.gpio_pin, "up");   // AM2302-style
        ctx->inited = true;

    } else {
        // Unknown → fallback to digital
        gpio_init((uint)ctx->g.gpio_pin);
        gpio_set_dir((uint)ctx->g.gpio_pin, GPIO_IN);
        apply_pull((uint)ctx->g.gpio_pin, ctx->g.gpio_pull);

        uint8_t raw = (uint8_t)gpio_get((uint)ctx->g.gpio_pin);
        if (ctx->g.gpio_invert) raw = !raw;

        ctx->last_raw    = raw;
        ctx->last_state  = raw;
        ctx->last_change = get_absolute_time();
        ctx->raw_change  = ctx->last_change;
        ctx->counter     = 0;
        ctx->inited      = true;
    }

    *ctx_out = ctx;
    return 0;
}



/* ----------------------------------------------------------------------------
 * bus_if: probe
 * --------------------------------------------------------------------------*/
static int gpio_probe(void *ctx_)
{
    gpio_ctx_t *ctx = (gpio_ctx_t*)ctx_;
    if (!ctx || !ctx->inited) return -1;

    if (mode_is_digital(ctx) || mode_is_counter(ctx) || mode_is_onewire(ctx)) {
        (void)gpio_get((uint)ctx->g.gpio_pin);
        return 0;
    } else if (mode_is_pulse(ctx)) {
        int us = measure_pulse_us((uint)ctx->g.gpio_trig,
                                  (uint)ctx->g.gpio_echo,
                                  (uint32_t)ctx->g.gpio_trig_us,
                                  (uint32_t)ctx->g.gpio_pulse_timeout_us);
        (void)us; // timeout is still "sensor present"
        return 0;
    }
    return 0;
}


/* ----------------------------------------------------------------------------
 * bus_if: poll_once
 *
 * DIGITAL mode payload (len=3):
 *   [0] = 0x01 if HIGH, 0x00 if LOW
 *   [1..2] = duration (ms) that the current state has been held (little-endian)
 *
 * COUNTER mode payload (len=4):
 *   [0..3] = edge counter (uint32 LE) since init
 *
 * PULSE/PWM mode payload:
 *   If pulse width captured:
 *       len=3, [0..1]=width_us (u16 LE, clamped), [2]=0x01 (valid flag)
 *   If timeout:
 *       len=1, [0]=0x00
 *
 * ONEWIRE mode payload (len=4 on success):
 *   [0..1] = humidity_x10 (u16 LE)
 *   [2..3] = temp_x10 (s16 LE)
 *   If read fails: returns 0 (no sample this tick)
 * --------------------------------------------------------------------------*/
static int gpio_poll_once(void *ctx_, uint8_t *buf, size_t cap)
{
    gpio_ctx_t *ctx = (gpio_ctx_t*)ctx_;
    if (!ctx || !ctx->inited || !buf || cap == 0) return -1;

    if (mode_is_digital(ctx)) {
        if (cap < 3) return -2;

        uint8_t st = debounce_read(ctx);
        uint32_t dur = (uint32_t)(
            to_ms_since_boot(get_absolute_time()) -
            to_ms_since_boot(ctx->last_change)
        );

        buf[0] = st ? 0x01 : 0x00;
        buf[1] = (uint8_t)(dur & 0xFF);
        buf[2] = (uint8_t)((dur >> 8) & 0xFF);
        return 3;

    } else if (mode_is_counter(ctx)) {
        if (cap < 4) return -2;

        uint8_t raw = read_input((uint)ctx->g.gpio_pin, ctx->g.gpio_invert != 0);
        if (raw != ctx->last_raw) {
            // simple edge counter (both rising and falling)
            ctx->counter++;
            ctx->last_raw = raw;
        }

        uint32_t c = ctx->counter;
        buf[0] = (uint8_t)(c & 0xFF);
        buf[1] = (uint8_t)((c >> 8) & 0xFF);
        buf[2] = (uint8_t)((c >> 16) & 0xFF);
        buf[3] = (uint8_t)((c >> 24) & 0xFF);
        return 4;

    } else if (mode_is_pulse(ctx)) {
        int us = measure_pulse_us((uint)ctx->g.gpio_trig,
                                  (uint)ctx->g.gpio_echo,
                                  (uint32_t)ctx->g.gpio_trig_us,
                                  (uint32_t)ctx->g.gpio_pulse_timeout_us);
        if (us <= 0) {
            if (cap < 1) return -2;
            buf[0] = 0x00;   // timeout/no pulse
            return 1;
        } else {
            if (cap < 3) return -2;
            if (us > 65535) us = 65535;
            buf[0] = (uint8_t)(us & 0xFF);
            buf[1] = (uint8_t)((us >> 8) & 0xFF);
            buf[2] = 0x01;   // “valid” tag
            return 3;
        }

    } else if (mode_is_onewire(ctx)) {
        // DHT-style one-wire: hum_x10 (u16 LE), temp_x10 (s16 LE)
        if (cap < 4) return -2;

        uint16_t hum10 = 0;
        int16_t  t10   = 0;

        if (onewire_dht_read((uint)ctx->g.gpio_pin, &hum10, &t10) != 0) {
            // read failed — skip this sample
            return 0;
        }

        buf[0] = (uint8_t)(hum10 & 0xFF);
        buf[1] = (uint8_t)((hum10 >> 8) & 0xFF);
        buf[2] = (uint8_t)(t10 & 0xFF);
        buf[3] = (uint8_t)((t10 >> 8) & 0xFF);
        return 4;
    }

    // Fallback: treat as digital
    if (cap < 3) return -2;
    uint8_t st = debounce_read(ctx);
    uint32_t dur = (uint32_t)(
        to_ms_since_boot(get_absolute_time()) -
        to_ms_since_boot(ctx->last_change)
    );

    buf[0] = st ? 0x01 : 0x00;
    buf[1] = (uint8_t)(dur & 0xFF);
    buf[2] = (uint8_t)((dur >> 8) & 0xFF);
    return 3;
}

/* ----------------------------------------------------------------------------
 * bus_if: stream start/stop — nothing special in this GPIO driver
 * --------------------------------------------------------------------------*/
static void gpio_stream_start(void *ctx) { (void)ctx; }
static void gpio_stream_stop(void *ctx)  { (void)ctx; }

/* ----------------------------------------------------------------------------
 * Bus descriptor
 * --------------------------------------------------------------------------*/
const bus_t bus_gpio = {
    .name         = "gpio",
    .init_cfg     = gpio_init_cfg,
    .probe        = gpio_probe,
    .poll_once    = gpio_poll_once,
    .stream_start = gpio_stream_start,
    .stream_stop  = gpio_stream_stop
};
