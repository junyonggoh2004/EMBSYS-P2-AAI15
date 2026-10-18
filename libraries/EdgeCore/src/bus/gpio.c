#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "hal/hal.h"

#include "bus/bus_common.h"
#include "bus/bus_if.h"

/* ----------------------------------------------------------------------------
 * Context per GPIO sensor
 * --------------------------------------------------------------------------*/
typedef struct {
    app_cfg_t      g;           // full parsed config snapshot for this sensor
    uint64_t       last_change; // hal_time_us() of the last accepted change
    uint8_t        last_state;  // debounced state (for digital/counter)
    uint8_t        last_raw;    // last raw GPIO value
    uint32_t       counter;     // edge counter (for "counter" mode)
    bool           irq_count;   // counter mode: edges counted by interrupt
    bool           inited;
} gpio_ctx_t;


/* ----------------------------------------------------------------------------
 * Small helpers
 * --------------------------------------------------------------------------*/
static inline uint32_t ms_of(uint64_t us) {
    return (uint32_t)(us / 1000);
}

static inline uint8_t read_input(int pin, bool invert) {
    uint8_t v = hal_gpio_read(pin) > 0 ? 1 : 0;   // a refused pin reads low, as on the Pico
    return invert ? (uint8_t)(!v) : v;
}

/* Debounce: return stable state, update last_change when it toggles */
static uint8_t debounce_read(gpio_ctx_t *c) {
    const uint32_t db = (c->g.gpio_debounce_ms > 0) ? (uint32_t)c->g.gpio_debounce_ms : 0;
    const uint8_t  cur = read_input(c->g.gpio_pin, c->g.gpio_invert != 0);

    if (db == 0) {
        // no debouncing requested
        if (cur != c->last_state) {
            c->last_state  = cur;
            c->last_change = hal_time_us();
        }
        return c->last_state;
    }

    if (cur != c->last_state) {
        // change detected; only accept if duration >= debounce window
        const uint32_t elapsed = (uint32_t)(
            hal_time_ms() - ms_of(c->last_change)
        );
        if (elapsed >= db) {
            c->last_state  = cur;
            c->last_change = hal_time_us();
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
static void set_input(int pin, const char *pull_str) {
    if (pull_str && strcmp(pull_str, "up") == 0) {
        hal_gpio_mode(pin, HAL_GPIO_INPUT_PULLUP);
    } else if (pull_str && strcmp(pull_str, "down") == 0) {
        hal_gpio_mode(pin, HAL_GPIO_INPUT_PULLDOWN);
    } else {
        // "off", missing or unknown: no pull
        hal_gpio_mode(pin, HAL_GPIO_INPUT);
    }
}

/* Measure a pulse on echo_pin after driving trig_pin */
static int measure_pulse_us(int trig_pin, int echo_pin,
                            uint32_t trig_us, uint32_t timeout_us)
{
    if (trig_us == 0)        trig_us        = 10;
    if (timeout_us == 0)     timeout_us     = 30000;

    // ensure both are initialised
    hal_gpio_mode(trig_pin, HAL_GPIO_OUTPUT);   // driven low
    (void)echo_pin;   // input mode and pull are set once in init_cfg

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

/* ----------------------------------------------------------------------------
 * Generic DHT-style one-wire read (AM2302 / DHT22 framing)
 *   - pin: data pin with pull-up
 *   - humidity_x10: 0.1 %RH
 *   - temp_x10: 0.1 °C (signed)
 * Returns 0 on success, non-zero on error.
 * --------------------------------------------------------------------------*/
static int onewire_dht_read(int pin, uint16_t *humidity_x10, int16_t *temp_x10)
{
    if (!humidity_x10 || !temp_x10) return -1;

    // Drive start signal
    hal_gpio_mode(pin, HAL_GPIO_OUTPUT);   // driven low
    hal_delay_ms(2);     // >1 ms start
    hal_gpio_write(pin, 1);
    hal_delay_us(30);
    hal_gpio_mode(pin, HAL_GPIO_INPUT_PULLUP);

    // Wait for sensor response: ~80us low, then 80us high
    uint64_t t0 = hal_time_us();
    while (hal_gpio_read(pin) > 0) {
        if (hal_time_us() - t0 > 200u)
            return -2; // no response
    }
    while (hal_gpio_read(pin) <= 0) {
        if (hal_time_us() - t0 > 400u)
            return -3; // stuck low
    }
    while (hal_gpio_read(pin) > 0) {
        if (hal_time_us() - t0 > 600u)
            return -4; // stuck high
    }

    // Now 40 bits: 50 us low + (26..28 us high => 0, ~70 us high => 1)
    uint8_t data[5] = {0,0,0,0,0};

    for (int i = 0; i < 40; i++) {
        // wait for 50us low period
        uint64_t t_low = hal_time_us();
        while (hal_gpio_read(pin) <= 0) {
            if (hal_time_us() - t_low > 200u)
                return -5;
        }

        // measure high duration
        uint64_t t_high = hal_time_us();
        while (hal_gpio_read(pin) > 0) {
            if (hal_time_us() - t_high > 200u)
                break;
        }
        int high_us = (int)(hal_time_us() - t_high);

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

    // Refuse missing or nonexistent pins instead of guessing (D-02, D-08)
    if (mode_is_pulse(ctx)) {
        if (ctx->g.gpio_trig < 0 || ctx->g.gpio_echo < 0) {
            printf("[GPIO] %s: gpio.trig and gpio.echo are required\n", ctx->g.name);
            free(ctx);
            return -3;
        }
        if (!hal_gpio_valid(ctx->g.gpio_trig) || !hal_gpio_valid(ctx->g.gpio_echo)) {
            printf("[GPIO] %s: pin %d or %d not available on this board\n",
                   ctx->g.name, ctx->g.gpio_trig, ctx->g.gpio_echo);
            free(ctx);
            return -3;
        }
    } else {
        if (ctx->g.gpio_pin < 0) {
            printf("[GPIO] %s: gpio.pin is required\n", ctx->g.name);
            free(ctx);
            return -3;
        }
        if (!hal_gpio_valid(ctx->g.gpio_pin)) {
            printf("[GPIO] %s: pin %d not available on this board\n", ctx->g.name, ctx->g.gpio_pin);
            free(ctx);
            return -3;
        }
    }
    if (ctx->g.gpio_debounce_ms <= 0)   ctx->g.gpio_debounce_ms = 50;

    // Pulse defaults
    if (ctx->g.gpio_trig_us <= 0)          ctx->g.gpio_trig_us          = 10;
    if (ctx->g.gpio_pulse_timeout_us <= 0) ctx->g.gpio_pulse_timeout_us = 30000;

    // --------- GPIO init according to mode ----------
    if (mode_is_digital(ctx) || mode_is_counter(ctx)) {
        set_input(ctx->g.gpio_pin, ctx->g.gpio_pull);

        uint8_t raw = read_input(ctx->g.gpio_pin, ctx->g.gpio_invert != 0);

        ctx->last_raw    = raw;
        ctx->last_state  = raw;
        ctx->last_change = hal_time_us();
        ctx->counter     = 0;
        // Counter mode counts every edge by interrupt; the reference only saw
        // one change per sample, so faster signals lost edges (task 15, T-05).
        ctx->irq_count   = mode_is_counter(ctx) &&
                           hal_gpio_count_edges(ctx->g.gpio_pin, true) == HAL_OK;
        ctx->inited      = true;

    } else if (mode_is_pulse(ctx)) {
        hal_gpio_mode(ctx->g.gpio_trig, HAL_GPIO_OUTPUT);   // driven low
        // gpio.pull applies to Echo: an unconnected S3 pin can float high,
        // which reads as a full-timeout echo (D-07).
        set_input(ctx->g.gpio_echo, ctx->g.gpio_pull);

        ctx->inited = true;

    } else if (mode_is_onewire(ctx)) {
        // Generic 1-wire: just set input with pull-up.
        set_input(ctx->g.gpio_pin, "up");   // AM2302-style
        ctx->inited = true;

    } else {
        // Unknown → fallback to digital
        set_input(ctx->g.gpio_pin, ctx->g.gpio_pull);

        uint8_t raw = read_input(ctx->g.gpio_pin, ctx->g.gpio_invert != 0);

        ctx->last_raw    = raw;
        ctx->last_state  = raw;
        ctx->last_change = hal_time_us();
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
        (void)hal_gpio_read(ctx->g.gpio_pin);
        return 0;
    } else if (mode_is_pulse(ctx)) {
        int us = measure_pulse_us(ctx->g.gpio_trig,
                                  ctx->g.gpio_echo,
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
        uint32_t dur = hal_time_ms() - ms_of(ctx->last_change);

        buf[0] = st ? 0x01 : 0x00;
        buf[1] = (uint8_t)(dur & 0xFF);
        buf[2] = (uint8_t)((dur >> 8) & 0xFF);
        return 3;

    } else if (mode_is_counter(ctx)) {
        if (cap < 4) return -2;

        if (ctx->irq_count) {
            ctx->counter = hal_gpio_edge_count(ctx->g.gpio_pin);
        } else {
            uint8_t raw = read_input(ctx->g.gpio_pin, ctx->g.gpio_invert != 0);
            if (raw != ctx->last_raw) {
                // fallback: one change per sample (both rising and falling)
                ctx->counter++;
                ctx->last_raw = raw;
            }
        }

        uint32_t c = ctx->counter;
        buf[0] = (uint8_t)(c & 0xFF);
        buf[1] = (uint8_t)((c >> 8) & 0xFF);
        buf[2] = (uint8_t)((c >> 16) & 0xFF);
        buf[3] = (uint8_t)((c >> 24) & 0xFF);
        return 4;

    } else if (mode_is_pulse(ctx)) {
        int us = measure_pulse_us(ctx->g.gpio_trig,
                                  ctx->g.gpio_echo,
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

        if (onewire_dht_read(ctx->g.gpio_pin, &hum10, &t10) != 0) {
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
    uint32_t dur = hal_time_ms() - ms_of(ctx->last_change);

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
