// scheduler.c — multi-sensor (I2C0 + I2C1 + UART + GPIO) with <pico/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hal/hal.h"
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
    uint64_t next_run;          // hal_time_us() deadline, 0 = not armed
    uint32_t overruns;          // samples skipped because the scheduler fell behind
    uint32_t last_overrun_log_ms;
    bool in_use;
} active_sensor_t;

static active_sensor_t g_sensors[MAX_ACTIVE];

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
    SLOG("reset: cleared %d slots", (int)MAX_ACTIVE);
}
void scheduler_dump(void) {
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (g_sensors[i].in_use) {
            const app_cfg_t *c = &g_sensors[i].cfg_copy;
            printf("[SCHED] #%d name=%s proto=%s mode=%s freq=%.3fHz\n",
                   i, c->name,
                   proto_name(c->proto) ? proto_name(c->proto) : "unknown",
                   (c->mode==sample_mode_stream)?"stream":"poll",
                   c->freq_hz);
            if (c->proto == proto_spi) {
                printf("[SCHED] #%d SPI host=%s sclk=%d mosi=%d miso=%d cs=%d hz=%u mode=%u tx=%u rx=%u timeout_ms=%u\n",
                       i, c->spi_host, c->spi_sclk, c->spi_mosi, c->spi_miso,
                       c->spi_cs, (unsigned)c->spi_hz, (unsigned)c->spi_mode,
                       (unsigned)c->spi_tx_len, (unsigned)c->spi_read_len,
                       (unsigned)c->spi_timeout_ms);
            }
        }
    }
}
/* Add one config block as a new active sensor */
int scheduler_add_config(const app_cfg_t *cfg_in) {
    if (cfg_in->cfg_error[0]) {
        printf("[SCHED] config %s refused: %s\n", cfg_in->name, cfg_in->cfg_error);
        return -5;
    }
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (!g_sensors[i].in_use) {
            active_sensor_t *S = &g_sensors[i];
            memset(S, 0, sizeof(*S));
            S->cfg_copy = *cfg_in;
            S->in_use = true;
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
                S->next_run = hal_time_us();  // not really used for remote
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
            S->next_run = hal_time_us();

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

/* Tick all sensors */
void scheduler_run_all(bool running) {
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

    static bool s_prev_running = false;
    if (running && !s_prev_running) {
        uint64_t now_edge = hal_time_us();
        for (int j = 0; j < MAX_ACTIVE; j++) if (g_sensors[j].in_use) {
            g_sensors[j].next_run = now_edge;       // arm all to “now”
            SLOG("arm[%d/%s]: next_run reset to NOW", j, g_sensors[j].cfg_copy.name);
        }
    }
    s_prev_running = running;

    // extra safety: if running but a sensor next_run looks bogus, arm it
    if (running) {
        uint64_t now_chk = hal_time_us();
        for (int j = 0; j < MAX_ACTIVE; j++) if (g_sensors[j].in_use) {
            int64_t dt_chk = (int64_t)(g_sensors[j].next_run - now_chk); // to - from
            if (g_sensors[j].next_run == 0 || dt_chk < -(10LL*1000*1000)) { // >10s late or NIL
                g_sensors[j].next_run = now_chk;
                SLOG("arm[%d/%s]: corrected next_run -> NOW", j, g_sensors[j].cfg_copy.name);
            }
        }
    }

    uint64_t now = hal_time_us();

    for (int i = 0; i < MAX_ACTIVE; i++) {
        active_sensor_t *S = &g_sensors[i];
        if (!S->in_use || !S->bus) continue;

        const app_cfg_t *cfg = &S->cfg_copy;

        // Stream transitions
        if (cfg->mode == sample_mode_stream) {
            if (running && !S->streaming) {
                SLOG("stream[%d/%s]: START", i, cfg->name);
                if (S->bus->stream_start) S->bus->stream_start(S->ctx);
                S->streaming = true;
            } else if (!running && S->streaming) {
                SLOG("stream[%d/%s]: STOP", i, cfg->name);
                if (S->bus->stream_stop) S->bus->stream_stop(S->ctx);
                S->streaming = false;
            }
        } else if (S->streaming) {
            SLOG("stream[%d/%s]: STOP (leaving stream mode)", i, cfg->name);
            if (S->bus->stream_stop) S->bus->stream_stop(S->ctx);
            S->streaming = false;
        }

        if (!running) continue;

        int n = 0;                 // neutral (no error unless we polled)
        bool did_poll = false;

if (cfg->mode == sample_mode_poll) {
    int64_t dt_us = (int64_t)(S->next_run - now); // to - from
    if (dt_us <= 0) {
        // More than one period late means at least one sample was skipped:
        // report it (rate-limited) instead of slowing down silently (T-04).
        uint32_t late_ms = (uint32_t)(-dt_us / 1000);
        float fp = cfg->freq_hz > 0.0f ? 1000.0f / cfg->freq_hz : 1000.0f;
        if (S->next_run != 0 && late_ms > (uint32_t)(fp * 1.1f)) {
            S->overruns++;
            uint32_t now_ms = hal_time_ms();
            if (now_ms - S->last_overrun_log_ms >= 5000u) {
                printf("[SCHED] overload: %s ran %u ms late (period %u ms, %u skipped so far)\n",
                       cfg->name, (unsigned)late_ms, (unsigned)(fp + 0.5f), (unsigned)S->overruns);
                S->last_overrun_log_ms = now_ms;
            }
        }
        did_poll = true;
        n = S->bus->poll_once ? S->bus->poll_once(S->ctx, buf, sizeof(buf)) : -1;

        // ---- float-aware period_ms ----
        float f = cfg->freq_hz;
        uint32_t period_ms;
        if (f > 0.0f) {
            float p_ms_f = 1000.0f / f;     // e.g. 0.5 Hz -> 2000ms
            if (p_ms_f < 1.0f)      p_ms_f = 1.0f;        // cap minimum
            if (p_ms_f > 600000.0f) p_ms_f = 600000.0f;   // cap ~10 minutes
            period_ms = (uint32_t)(p_ms_f + 0.5f);        // round
        } else {
            period_ms = 1000u;  // default 1s if freq=0
        }

        now = hal_time_us();
        S->next_run = now + (uint64_t)period_ms * 1000u;
    }
}

else {
            // stream mode: poll every tick
            SLOG("stream[%d/%s]: poll_once()", i, cfg->name);
            did_poll = true;
            n = S->bus->poll_once ? S->bus->poll_once(S->ctx, buf, sizeof(buf)) : -1;
            SLOG("stream[%d/%s]: result=%d", i, cfg->name, n);
        }

        if (did_poll) {
            if (n > 0) {
                const char *p = proto_name(cfg->proto);
                SLOG("publish[%d/%s]: %d bytes via %s", i, cfg->name, n, p);
                publisher_emit_hex(cfg, p, cfg->name, buf, n);
            } else if (n < 0) {
                SLOG("error[%d/%s]: poll returned %d", i, cfg->name, n);
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
    if (!running) return max_ms;
    uint64_t now = hal_time_us();
    uint32_t wait = max_ms;
    for (int i = 0; i < MAX_ACTIVE; i++) {
        const active_sensor_t *S = &g_sensors[i];
        if (!S->in_use || !S->bus) continue;
        if (S->cfg_copy.mode == sample_mode_stream) {
            if (wait > 10) wait = 10;
            continue;
        }
        if (S->next_run <= now) return 0;
        uint64_t ms = (S->next_run - now + 999) / 1000;
        if (ms < wait) wait = (uint32_t)ms;
    }
    return wait;
}
