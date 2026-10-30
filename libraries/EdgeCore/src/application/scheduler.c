// scheduler.c — multi-sensor (I2C0 + I2C1 + UART + GPIO) with <pico/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include "hal/hal.h"
#include "application/scheduler.h"
#include "application/pulse_sequence.h"
#include "application/output_format.h"
#include "bus/bus_common.h"
#include "bus/bus_if.h"
#include "mqtt/mqtt_telemetry.h"
#define MAX_ACTIVE 8

/* ===================== Debug logging ===================== */
#ifndef SCHED_DEBUG
#define SCHED_DEBUG 1
#endif

#if SCHED_DEBUG
#define SLOG(fmt, ...) do { \
    uint32_t _ms = hal_time_ms(); \
    printf("[SCHED %9ums] " fmt "\n", _ms, ##__VA_ARGS__); \
} while(0)
#else
#define SLOG(...) do{}while(0)
#endif
/* ========================================================= */

typedef struct {
    const bus_t *bus;
    void *ctx;
    app_cfg_t cfg_copy;         // keep a private copy
    bool streaming;
    bool pending;
    timing_state_t timing;
    uint32_t overruns;          // samples skipped because the scheduler fell behind
    uint32_t last_overrun_log_ms;
    uint32_t acquisition_errors, last_error_log_ms;
    bool in_use;
} active_sensor_t;

static active_sensor_t g_sensors[MAX_ACTIVE];
static bool s_prev_running;
static pulse_sequence_t s_pair;
static unsigned s_pair_slots[2];
static bool s_pair_reported = true;

static void pair_report(void)
{
    if (s_pair_reported || pulse_sequence_active(&s_pair)) return;
    printf("[PAIR] state=%s outcome=%u elapsed_us=%llu window_us=%u\n",
           s_pair.state == PULSE_SEQUENCE_COMPLETE ? "complete" :
           s_pair.state == PULSE_SEQUENCE_CANCELLED ? "cancelled" : "failed",
           (unsigned)s_pair.outcome,
           (unsigned long long)(s_pair.finished_us - s_pair.started_us),
           (unsigned)s_pair.config.window_us);
    for (unsigned i = 0; i < 2; ++i) {
        const pulse_sequence_sample_t *sample = &s_pair.samples[i];
        printf("[PAIR] sensor=%c name=%s attempted=%u valid=%u status=%d rise_us=%llu fall_us=%llu width_us=%u\n",
               'A' + i, g_sensors[s_pair_slots[i]].cfg_copy.name,
               sample->attempted ? 1u : 0u, sample->valid ? 1u : 0u, sample->status,
               (unsigned long long)sample->pulse.rise_us,
               (unsigned long long)sample->pulse.fall_us, (unsigned)sample->pulse.width_us);
    }
    s_pair_reported = true;
}

void scheduler_pair_cancel(void)
{
    pulse_sequence_cancel(&s_pair, hal_time_us());
    pair_report();
}

/* Map enum -> name used by bus_lookup */
static inline const char* proto_name(proto_t p) {
    switch (p) {
        case proto_i2c:  return "i2c";
        case proto_uart: return "uart";
        case proto_gpio: return "gpio";
        case proto_mqtt: return "mqtt";
        case proto_spi:  return "spi";
        default:         return NULL;
    }
}

int scheduler_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_ACTIVE; i++) if (g_sensors[i].in_use) n++;
    return n;
}

/* Initialize scheduler */
void scheduler_reset(void) {
    scheduler_pair_cancel();
    memset(&s_pair, 0, sizeof s_pair);
    /* Contexts are opaque to the scheduler. Give each driver a chance to
       stop hardware activity and release platform resources before CLEAR. */
    for (int i = 0; i < MAX_ACTIVE; i++) {
        active_sensor_t *S = &g_sensors[i];
        if (S->streaming && S->bus && S->bus->stream_stop) {
            S->bus->stream_stop(S->ctx);
        }
        if (S->ctx) {
            if (S->bus && S->bus->deinit) S->bus->deinit(S->ctx);
            else free(S->ctx); /* compatibility for an out-of-tree driver */
        }
    }
    memset(g_sensors, 0, sizeof(g_sensors));
    s_prev_running = false;
    SLOG("reset: cleared %d slots", (int)MAX_ACTIVE);
}
void scheduler_dump(void) {
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (g_sensors[i].in_use) {
            const app_cfg_t *c = &g_sensors[i].cfg_copy;
            printf("[SCHED] #%d name=%s proto=%s mode=%s freq=%.3fHz timing=%s interval=%ums timeout=%ums window=%ums trigger=%s status=%u\n",
                   i, c->name,
                   proto_name(c->proto) ? proto_name(c->proto) : "unknown",
                   (c->mode==sample_mode_stream)?"stream":"poll",
                   c->freq_hz, timing_mode_name(c->timing.mode),
                   (unsigned)c->timing.interval_ms, (unsigned)c->timing.timeout_ms,
                   (unsigned)c->timing.window_ms,
                   c->timing_trigger_source[0] ? c->timing_trigger_source : "-",
                   (unsigned)g_sensors[i].timing.status);
            if (c->proto == proto_spi) {
                printf("[SCHED] #%d SPI host=%s sclk=%d mosi=%d miso=%d cs=%d hz=%u mode=%u tx=%u rx=%u timeout_ms=%u\n",
                       i, c->spi_host, c->spi_sclk, c->spi_mosi, c->spi_miso,
                       c->spi_cs, (unsigned)c->spi_hz, (unsigned)c->spi_mode,
                       (unsigned)c->spi_tx_len, (unsigned)c->spi_read_len,
                       (unsigned)c->spi_timeout_ms);
            }
            printf("[SENSOR] name=%s pending=%u timeouts=%u errors=%u overruns=%u\n",
                   c->name, g_sensors[i].pending ? 1u : 0u,
                   (unsigned)g_sensors[i].timing.timeouts,
                   (unsigned)g_sensors[i].acquisition_errors,
                   (unsigned)g_sensors[i].overruns);
        }
    }
}
/* Add one config block as a new active sensor */
static bool pulse_config(const app_cfg_t *c)
{
    return c->proto == proto_gpio && c->gpio_mode && !strcmp(c->gpio_mode, "pulse");
}

static int pair_start(void *context, unsigned sensor, uint32_t timeout_us)
{
    (void)context;
    const app_cfg_t *c = &g_sensors[s_pair_slots[sensor]].cfg_copy;
    return hal_gpio_capture_start(c->gpio_trig, c->gpio_echo,
                                  c->gpio_trig_us > 0 ? (uint32_t)c->gpio_trig_us : 10u,
                                  timeout_us);
}
static int pair_poll(void *context, unsigned sensor, hal_pulse_result_t *pulse)
{
    (void)context;
    return hal_gpio_capture_poll(g_sensors[s_pair_slots[sensor]].cfg_copy.gpio_echo, pulse);
}
static void pair_cancel(void *context, unsigned sensor)
{
    (void)context;
    hal_gpio_capture_cancel(g_sensors[s_pair_slots[sensor]].cfg_copy.gpio_echo);
}

int scheduler_pair_command(const char *args)
{
    if (!args) return HAL_ERR_INVALID;
    if (s_prev_running || pulse_sequence_active(&s_pair)) return HAL_ERR_BUSY;
    char names[2][32], order[2], guard[32], extra;
    if (sscanf(args, "%31s %31s %1s %31s %c", names[0], names[1], order, guard, &extra) != 4 ||
        (order[0] != 'A' && order[0] != 'B')) return HAL_ERR_INVALID;
    for (const char *p = guard; *p; ++p) if (!isdigit((unsigned char)*p)) return HAL_ERR_INVALID;
    errno = 0;
    unsigned long guard_us = strtoul(guard, NULL, 10);
    if (errno == ERANGE || guard_us > UINT32_MAX) return HAL_ERR_INVALID;
    unsigned slots[2] = {MAX_ACTIVE, MAX_ACTIVE};
    for (unsigned i = 0; i < MAX_ACTIVE; ++i) {
        if (!g_sensors[i].in_use) continue;
        for (unsigned n = 0; n < 2; ++n) {
            if (!strcmp(g_sensors[i].cfg_copy.name, names[n])) {
                if (slots[n] != MAX_ACTIVE) return HAL_ERR_INVALID; // ambiguous name
                slots[n] = i;
            }
        }
    }
    pulse_sequence_config_t config = {
        .first = order[0] == 'A' ? 0u : 1u, .guard_us = (uint32_t)guard_us,
        .window_us = PULSE_SEQUENCE_WINDOW_MAX_US,
        .error_policy = PULSE_SEQUENCE_FAIL_FAST // diagnostic stops on its first error
    };
    if (slots[0] == MAX_ACTIVE || slots[1] == MAX_ACTIVE || slots[0] == slots[1])
        return HAL_ERR_INVALID;
    for (unsigned n = 0; n < 2; ++n) {
        const active_sensor_t *sensor = &g_sensors[slots[n]];
        if (!pulse_config(&sensor->cfg_copy) || sensor->pending) return HAL_ERR_INVALID;
        config.timeout_us[n] = sensor->cfg_copy.gpio_pulse_timeout_us > 0 ?
                               (uint32_t)sensor->cfg_copy.gpio_pulse_timeout_us : 30000u;
    }
    const pulse_sequence_ops_t ops = {pair_start, pair_poll, pair_cancel};
    int rc = pulse_sequence_begin(&s_pair, &config, &ops, NULL, hal_time_us());
    if (rc != HAL_OK) return rc;
    memcpy(s_pair_slots, slots, sizeof slots);
    s_pair_reported = false;
    printf("[PAIR] armed A=%s B=%s first=%s guard_us=%u window_us=%u\n",
           names[0], names[1], order, (unsigned)config.guard_us, (unsigned)config.window_us);
    return HAL_OK;
}

static size_t config_pins(const app_cfg_t *c, int pins[4])
{
    switch (c->proto) {
    case proto_gpio:
        if (pulse_config(c)) { pins[0] = c->gpio_trig; pins[1] = c->gpio_echo; return 2; }
        pins[0] = c->gpio_pin; return 1;
    case proto_i2c: pins[0] = c->i2c_sda; pins[1] = c->i2c_scl; return 2;
    case proto_uart: pins[0] = c->uart_tx; pins[1] = c->uart_rx; return 2;
    case proto_spi:
        pins[0] = c->spi_sclk; pins[1] = c->spi_mosi;
        pins[2] = c->spi_miso; pins[3] = c->spi_cs; return 4;
    default: return 0;
    }
}

int scheduler_add_config(const app_cfg_t *cfg_in) {
    const char *timing_error = NULL;
    if (!cfg_in || !timing_config_validate(&cfg_in->timing, &timing_error)) {
        printf("[SCHED] invalid timing: %s\n", timing_error ? timing_error : "null config");
        return -5;
    }
    if (cfg_in->cfg_error[0]) {
        printf("[SCHED] config %s refused: %s\n", cfg_in->name, cfg_in->cfg_error);
        return -5;
    }
    // A new configuration must not repurpose an armed pulse pin. Shared I2C
    // and SPI bus pins remain allowed where neither sensor is a pulse device.
    int new_pins[4];
    size_t new_count = config_pins(cfg_in, new_pins);
    for (int i = 0; i < MAX_ACTIVE; ++i) {
        const active_sensor_t *existing = &g_sensors[i];
        if (!existing->in_use || (!pulse_config(cfg_in) && !pulse_config(&existing->cfg_copy))) continue;
        int old_pins[4];
        size_t old_count = config_pins(&existing->cfg_copy, old_pins);
        for (size_t a = 0; a < new_count; ++a) {
            for (size_t b = 0; b < old_count; ++b) {
                if (new_pins[a] >= 0 && new_pins[a] == old_pins[b]) {
                    printf("[SCHED] config %s refused: pulse pin %d is used by %s\n",
                           cfg_in->name, new_pins[a], existing->cfg_copy.name);
                    return -5;
                }
            }
        }
    }
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (!g_sensors[i].in_use) {
            active_sensor_t *S = &g_sensors[i];
            memset(S, 0, sizeof(*S));
            S->cfg_copy = *cfg_in;
            S->in_use = true;
            timing_state_init(&S->timing, &S->cfg_copy.timing, hal_time_us(), false);
            /* Special case: remote MQTT source – no local bus. */
            if (cfg_in->proto == proto_mqtt) {
                // Use remote_node / remote_source fields from cfg_in
                const char *remote_node   = cfg_in->remote_node;
                const char *remote_source = cfg_in->remote_source;

                if (!remote_node || !*remote_node ||
                    !remote_source || !*remote_source) {
                    printf("[SCHED] add[%d]: MQTT proto but missing remote_node/remote_source for %s\n",
                           i, cfg_in->name);
                    S->in_use = false;
                    return -3;
                }

                // For now: just tell MQTT layer to follow this remote source.
                if (!mqtt_follow_remote_source(remote_node, remote_source, cfg_in->name)) {
                    printf("[SCHED] add[%d]: mqtt_follow_remote_source FAILED for %s\n", i, cfg_in->name);
                    S->in_use = false;
                    return -4;
                }

                S->bus = NULL;
                S->ctx = NULL;
                printf("[SCHED] added MQTT-remote: local=%s <- %s/%s\n",
                       cfg_in->name, remote_node, remote_source);
                return i;
            }

            const char *pname = proto_name(cfg_in->proto);
            if (!pname) {
                SLOG("add[%d]: UNKNOWN proto for %s", i, cfg_in->name);
                S->in_use = false;
                return -1;
            }
            S->bus = bus_lookup(pname);
            if (!S->bus) {
                SLOG("add[%d]: UNKNOWN proto for %s", i, cfg_in->name);
                S->in_use = false;
                return -1;
            }

            // pass-through via sensor_cfg
            sensor_cfg_t scfg = {
                .name = S->cfg_copy.name,
                .proto = pname,
                .kv_pairs = (void *)&S->cfg_copy
            };

            void *new_ctx = NULL;
            int init_rc = S->bus->init_cfg ? S->bus->init_cfg(&scfg, &new_ctx) : -1;
            if (init_rc != 0) {
                /* A driver should normally clean up and return NULL on
                   failure. This also protects against a partially returned
                   context from a future driver such as SPI. */
                if (new_ctx) {
                    if (S->bus->deinit) S->bus->deinit(new_ctx);
                    else free(new_ctx);
                }
                SLOG("add[%d]: init_cfg FAILED for %s on %s", i, cfg_in->name, pname);
                S->bus = NULL;
                S->ctx = NULL;
                S->in_use = false;
                return -2;
            }
            S->ctx = new_ctx;

            if (S->bus->probe) (void)S->bus->probe(S->ctx);

            /* Nice one-line summary */
            if (cfg_in->proto == proto_i2c) {
                SLOG("add[%d]: %s on %s freq=%.3fHz mode=%s I2C(sda=%d,scl=%d,addr=0x%02X,reg=0x%X,len=%u)",
                     i, S->cfg_copy.name, pname, S->cfg_copy.freq_hz,
                     (S->cfg_copy.mode==sample_mode_stream)?"stream":"poll",
                     S->cfg_copy.i2c_sda, S->cfg_copy.i2c_scl, S->cfg_copy.i2c_addr,
                     S->cfg_copy.i2c_reg, S->cfg_copy.i2c_read_len);
            } else if (cfg_in->proto == proto_uart) {
                SLOG("add[%d]: %s on %s freq=%.3fHz mode=%s UART(tx=%d,rx=%d,baud=%u)",
                     i, S->cfg_copy.name, pname, S->cfg_copy.freq_hz,
                     (S->cfg_copy.mode==sample_mode_stream)?"stream":"poll",
                     S->cfg_copy.uart_tx, S->cfg_copy.uart_rx, S->cfg_copy.uart_baud);
            } else if (cfg_in->proto == proto_spi) {
                SLOG("add[%d]: %s on %s freq=%.3fHz mode=poll SPI(host=%s,sclk=%d,mosi=%d,miso=%d,cs=%d,hz=%u,mode=%u,tx=%u,rx=%u,timeout=%ums)",
                     i, S->cfg_copy.name, pname, S->cfg_copy.freq_hz,
                     S->cfg_copy.spi_host, S->cfg_copy.spi_sclk, S->cfg_copy.spi_mosi,
                     S->cfg_copy.spi_miso, S->cfg_copy.spi_cs, (unsigned)S->cfg_copy.spi_hz,
                     (unsigned)S->cfg_copy.spi_mode, (unsigned)S->cfg_copy.spi_tx_len,
                     (unsigned)S->cfg_copy.spi_read_len, (unsigned)S->cfg_copy.spi_timeout_ms);
            } else {
                SLOG("add[%d]: %s on %s freq=%.3fHz mode=%s method=%s GPIO(pin=%d)",
                     i, S->cfg_copy.name, pname, S->cfg_copy.freq_hz,
                     (S->cfg_copy.mode==sample_mode_stream)?"stream":"poll",
                     S->cfg_copy.gpio_mode,
                     S->cfg_copy.gpio_pin);
            }

            printf("[SCHED] added: %s on %s\n", S->cfg_copy.name, pname); // keep your legacy line too
            return i; // index
        }
    }
    SLOG("add: no free slots");
    printf("[SCHED] no free slots\n");
    return -10;
}

/* Backward-compatibility */
void scheduler_apply_config(const app_cfg_t *cfg) {
    SLOG("apply_config (compat): reset+add %s", cfg->name);
    scheduler_reset();
    (void)scheduler_add_config(cfg);
}

static int trigger_matching(const char *source, uint64_t timestamp_us, bool automatic) {
    if (!source || !*source) return 0;
    int armed = 0;
    for (int i = 0; i < MAX_ACTIVE; i++) {
        active_sensor_t *S = &g_sensors[i];
        if (!S->in_use || S->cfg_copy.timing.mode == TIMING_MODE_PERIODIC) continue;
        bool matches_name = strcmp(source, S->cfg_copy.name) == 0;
        bool matches_source = S->cfg_copy.timing_trigger_source[0] &&
                              strcmp(source, S->cfg_copy.timing_trigger_source) == 0;
        bool matches = automatic ? (S->cfg_copy.timing.mode == TIMING_MODE_EVENT_DRIVEN &&
                                     matches_source && !matches_name) :
                                    (matches_name || matches_source);
        if (matches && timing_state_trigger(&S->timing, timestamp_us)) {
            armed++;
            SLOG("trigger[%d/%s]: source=%s deadline=%llu", i, S->cfg_copy.name, source,
                 (unsigned long long)S->timing.deadline_us);
        }
    }
    return armed;
}

int scheduler_trigger(const char *source, uint64_t timestamp_us) {
    return trigger_matching(source, timestamp_us, false);
}

int scheduler_on_sample(const char *source, uint64_t timestamp_us) {
    return trigger_matching(source, timestamp_us, true);
}

/* Tick all sensors */
void scheduler_run_all(bool running) {
    if (pulse_sequence_active(&s_pair)) {
        if (running) scheduler_pair_cancel();
        else {
            pulse_sequence_step(&s_pair, hal_time_us());
            pair_report();
        }
    }
    uint8_t buf[128];
    {   // at most once per second, so a fast caller does not flood the console
        static uint32_t s_last_log_ms;
        static bool s_logged;
        uint32_t now_ms = hal_time_ms();
        if (!s_logged || now_ms - s_last_log_ms >= 1000u) {
            SLOG("run_all: running=%u", running?1u:0u);
            s_last_log_ms = now_ms;
            s_logged = true;
        }
    }

    if (running && !s_prev_running) {
        uint64_t now_edge = hal_time_us();
        for (int j = 0; j < MAX_ACTIVE; j++) if (g_sensors[j].in_use) {
            bool arm_now = g_sensors[j].cfg_copy.timing.mode != TIMING_MODE_EVENT_DRIVEN;
            timing_state_init(&g_sensors[j].timing, &g_sensors[j].cfg_copy.timing,
                              now_edge, arm_now);
            SLOG("arm[%d/%s]: mode=%s armed=%u", j, g_sensors[j].cfg_copy.name,
                 timing_mode_name(g_sensors[j].cfg_copy.timing.mode), arm_now ? 1u : 0u);
        }
    }
    s_prev_running = running;

    uint64_t now = hal_time_us();

    for (int i = 0; i < MAX_ACTIVE; i++) {
        active_sensor_t *S = &g_sensors[i];
        if (!S->in_use || !S->bus) continue;

        const app_cfg_t *cfg = &S->cfg_copy;
        bool active = running && cfg->timing.enabled;

        // Stream transitions
        if (cfg->mode == sample_mode_stream) {
            if (active && !S->streaming) {
                SLOG("stream[%d/%s]: START", i, cfg->name);
                if (S->bus->stream_start) S->bus->stream_start(S->ctx);
                S->streaming = true;
            } else if (!active && S->streaming) {
                SLOG("stream[%d/%s]: STOP", i, cfg->name);
                if (S->bus->stream_stop) S->bus->stream_stop(S->ctx);
                S->streaming = false;
            }
        } else if (S->streaming) {
            SLOG("stream[%d/%s]: STOP (leaving stream mode)", i, cfg->name);
            if (S->bus->stream_stop) S->bus->stream_stop(S->ctx);
            S->streaming = false;
        }

        if (!active) {
            if (S->pending && S->bus->cancel) S->bus->cancel(S->ctx);
            S->pending = false;
            continue;
        }

        int n = 0;                 // neutral (no error unless we polled)
        bool did_poll = false;

if (cfg->mode == sample_mode_poll) {
    now = hal_time_us(); /* prior sensor publication may have armed this one */
    timing_status_t timing_status = timing_state_poll(&S->timing, now);
    if (timing_status == TIMING_STATUS_TIMEOUT) {
        if (S->pending && S->bus->cancel) S->bus->cancel(S->ctx);
        S->pending = false;
        printf("[TIMING] sensor=%s status=timeout armed_us=%llu deadline_us=%llu\n",
               cfg->name, (unsigned long long)S->timing.armed_us,
               (unsigned long long)S->timing.deadline_us);
        timing_state_complete(&S->timing, now);
    } else if (timing_status == TIMING_STATUS_DUE) {
        int64_t dt_us = (int64_t)(S->timing.due_us - now); // due - now
        // More than one period late means at least one sample was skipped:
        // report it (rate-limited) instead of slowing down silently (T-04).
        uint32_t late_ms = (uint32_t)(-dt_us / 1000);
        uint32_t period_ms = cfg->timing.interval_ms;
        if (!S->pending && cfg->timing.mode == TIMING_MODE_PERIODIC && S->timing.due_us != 0 &&
            late_ms > (uint32_t)((float)period_ms * 1.1f)) {
            S->overruns++;
            uint32_t now_ms = hal_time_ms();
            if (now_ms - S->last_overrun_log_ms >= 5000u) {
                printf("[SCHED] overload: %s ran %u ms late (period %u ms, %u skipped so far)\n",
                       cfg->name, (unsigned)late_ms, (unsigned)period_ms, (unsigned)S->overruns);
                S->last_overrun_log_ms = now_ms;
            }
        }
        did_poll = true;
        n = S->bus->poll_once ? S->bus->poll_once(S->ctx, buf, sizeof(buf)) : -1;
        now = hal_time_us();
        if (timing_state_poll(&S->timing, now) == TIMING_STATUS_TIMEOUT) {
            if (S->bus->cancel) S->bus->cancel(S->ctx);
            printf("[TIMING] sensor=%s status=timeout phase=acquisition deadline_us=%llu\n",
                   cfg->name, (unsigned long long)S->timing.deadline_us);
            n = 0; /* a complete but late response is not a valid sample */
        }
        S->pending = n == BUS_POLL_PENDING;
        if (!S->pending) timing_state_complete(&S->timing, now);
    }
}

else {
            // stream mode: poll every tick
            SLOG("stream[%d/%s]: poll_once()", i, cfg->name);
            did_poll = true;
            n = S->bus->poll_once ? S->bus->poll_once(S->ctx, buf, sizeof(buf)) : -1;
            S->pending = n == BUS_POLL_PENDING;
            SLOG("stream[%d/%s]: result=%d", i, cfg->name, n);
        }

        if (did_poll) {
            if (n > 0) {
                const char *p = proto_name(cfg->proto);
                SLOG("publish[%d/%s]: %d bytes via %s", i, cfg->name, n, p);
                publisher_emit_hex(cfg, p, cfg->name, buf, n);
            } else if (n < 0 && n != BUS_POLL_PENDING) {
                ++S->acquisition_errors;
                uint32_t error_ms = hal_time_ms();
                if (S->acquisition_errors == 1 || error_ms - S->last_error_log_ms >= 1000u) {
                    printf("[SENSOR] name=%s error=%d total=%u\n", cfg->name, n,
                           (unsigned)S->acquisition_errors);
                    S->last_error_log_ms = error_ms;
                }
            }
        }
    }
}


void scheduler_run_step(const app_cfg_t *cfg, bool running) {
    (void)cfg;  // ignored — multi-sensor mode uses internal copies
    SLOG("run_step: enter (running=%u)", running?1u:0u);
    scheduler_run_all(running);
    SLOG("run_step: exit");
}

/* Milliseconds until the next poll-mode sensor falls due (0 if one is due
 * now), capped at max_ms. A streaming sensor keeps it at 10 ms. Lets an RTOS
 * task sleep exactly until there is work instead of a fixed loop delay. */
uint32_t scheduler_next_due_ms(bool running, uint32_t max_ms) {
    uint64_t now = hal_time_us();
    uint32_t wait = max_ms;
    if (pulse_sequence_active(&s_pair)) {
        uint32_t pair_wait = (pulse_sequence_wait_us(&s_pair, now, 1000000u) + 999u) / 1000u;
        if (pair_wait < wait) wait = pair_wait;
    }
    if (!running) return wait;
    for (int i = 0; i < MAX_ACTIVE; i++) {
        const active_sensor_t *S = &g_sensors[i];
        if (!S->in_use || !S->bus || !S->cfg_copy.timing.enabled) continue;
        if (S->pending) {
            if (wait > 1) wait = 1;
            continue;
        }
        if (S->cfg_copy.mode == sample_mode_stream) {
            if (wait > 10) wait = 10;
            continue;
        }
        uint32_t candidate = timing_state_wait_ms(&S->timing, now, wait);
        if (candidate < wait) wait = candidate;
    }
    return wait;
}
