// scheduler.c — multi-sensor (I2C0 + I2C1 + UART + GPIO) with <pico/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include "hal/hal.h"
#include "application/scheduler.h"
#include "application/pulse_sequence.h"
#include "application/entrance_window.h"
#include "application/event_identity.h"
#include "application/ld2450.h"
#include "application/output_format.h"
#include "application/sensor_measurement.h"
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
static entrance_window_t s_window;
static unsigned s_window_slots[3];
static ld2450_decoder_t s_radar;
static ld2450_zone_t s_zone;
static uint32_t s_radar_dropped;
static uint32_t s_radar_dropped_start;
static uint64_t s_radar_service_us;
static uint64_t s_window_warmup_until;

void scheduler_window_stop(void) { entrance_window_stop(&s_window, hal_time_us()); }

int scheduler_window_report(uint32_t token)
{
    if (s_prev_running || pulse_sequence_active(&s_pair) || s_window.pair_pending) return HAL_ERR_BUSY;
    const entrance_metrics_t *m = &s_window.metrics;
    bool configured = s_window.config.pair.window_us != 0;
    char p95[16], p99[16];
    uint32_t bounds[2] = {entrance_window_percentile_us(&s_window, 95), entrance_window_percentile_us(&s_window, 99)};
    if (bounds[0] == UINT32_MAX) strcpy(p95, ">50000");
    else snprintf(p95, sizeof p95, "%u", (unsigned)bounds[0]);
    if (bounds[1] == UINT32_MAX) strcpy(p99, ">50000");
    else snprintf(p99, sizeof p99, "%u", (unsigned)bounds[1]);
    printf("[WINDOW_REPORT] token=%08x kind=state version=1 configured=%u enabled=%u state=%u events=%u dropped=%u radar_frames=%u malformed=%u rx_dropped=%u rx_dropped_start=%u identity=%u event_schema=%u now_us=%llu\n",
           (unsigned)token, configured ? 1u : 0u, s_window.enabled ? 1u : 0u, (unsigned)s_window.state,
           (unsigned)s_window.events, (unsigned)s_window.drops, (unsigned)s_radar.frames,
           (unsigned)s_radar.malformed, (unsigned)s_radar_dropped, (unsigned)s_radar_dropped_start,
           event_identity_ready() ? 1u : 0u,
           EVENT_RECORD_SCHEMA_VERSION, (unsigned long long)hal_time_us());
    printf("[WINDOW_REPORT] token=%08x kind=timing pairs=%u valid=%u failed=%u cancelled=%u deadline_misses=%u skipped=%u min_us=%u mean_us=%u max_us=%u p95_le_us=%s p99_le_us=%s\n",
           (unsigned)token, (unsigned)m->pairs, (unsigned)m->valid, (unsigned)m->failed,
           (unsigned)m->cancelled, (unsigned)m->deadline_misses, (unsigned)m->skipped,
           (unsigned)m->min_us, m->pairs ? (unsigned)(m->sum_us / m->pairs) : 0u,
           (unsigned)m->max_us, p95, p99);
    printf("[WINDOW_REPORT] token=%08x kind=events entry=%u exit=%u incomplete=%u false_events=%u window_deadlines=%u radar_stale=%u window_faults=%u\n",
           (unsigned)token, (unsigned)m->entry, (unsigned)m->exit, (unsigned)m->incomplete,
           (unsigned)m->false_events, (unsigned)m->window_deadlines, (unsigned)m->radar_stale,
           (unsigned)m->window_faults);
    printf("[WINDOW_REPORT] token=%08x kind=config period_us=%u window_us=%u pair_limit_us=%u clear_us=%u radar_stale_us=%u max_pairs=%u guard_us=%u timeout_a_us=%u timeout_b_us=%u occupied_a_mm=%u occupied_b_mm=%u\n",
           (unsigned)token, ENTRANCE_PERIOD_US, ENTRANCE_WINDOW_US, PULSE_SEQUENCE_WINDOW_MAX_US,
           ENTRANCE_CLEAR_US, ENTRANCE_RADAR_STALE_US, ENTRANCE_MAX_PAIRS,
           (unsigned)s_window.config.pair.guard_us, (unsigned)s_window.config.pair.timeout_us[0],
           (unsigned)s_window.config.pair.timeout_us[1], (unsigned)s_window.config.occupied_mm[0],
           (unsigned)s_window.config.occupied_mm[1]);
    const app_cfg_t *a = &g_sensors[s_window_slots[0]].cfg_copy;
    const app_cfg_t *b = &g_sensors[s_window_slots[1]].cfg_copy;
    const app_cfg_t *r = &g_sensors[s_window_slots[2]].cfg_copy;
    printf("[WINDOW_REPORT] token=%08x kind=geometry a_name=%s a_trig=%d a_echo=%d b_name=%s b_trig=%d b_echo=%d radar_name=%s radar_tx=%d radar_rx=%d xmin=%ld xmax=%ld ymin=%ld ymax=%ld\n",
           (unsigned)token, configured ? a->name : "-", configured ? a->gpio_trig : -1,
           configured ? a->gpio_echo : -1, configured ? b->name : "-", configured ? b->gpio_trig : -1,
           configured ? b->gpio_echo : -1, configured ? r->name : "-", configured ? r->uart_tx : -1,
           configured ? r->uart_rx : -1, (long)s_zone.xmin, (long)s_zone.xmax, (long)s_zone.ymin, (long)s_zone.ymax);
    printf("[WINDOW_REPORT] token=%08x kind=end\n", (unsigned)token);
    return HAL_OK;
}

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
    scheduler_window_stop();
    memset(&s_window, 0, sizeof s_window);
    memset(&s_radar, 0, sizeof s_radar);
    memset(&s_zone, 0, sizeof s_zone);
    s_radar_dropped = s_radar_dropped_start = 0;
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
    printf("[WINDOW] enabled=%u state=%u events=%u dropped=%u radar_frames=%u malformed=%u rx_dropped=%u identity=%u\n",
           s_window.enabled ? 1u : 0u, (unsigned)s_window.state,
           (unsigned)s_window.events, (unsigned)s_window.drops, (unsigned)s_radar.frames,
           (unsigned)s_radar.malformed, (unsigned)s_radar_dropped, event_identity_ready() ? 1u : 0u);
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
            if (c->proto == proto_i2c) {
                printf("[SCHED] #%d I2C sensor=%s sda=%d scl=%d addr=0x%02X\n",i,
                       c->i2c_sensor==I2C_SENSOR_AHT ? "aht" : c->i2c_sensor==I2C_SENSOR_TSL2561 ? "tsl2561" : "raw",
                       c->i2c_sda,c->i2c_scl,c->i2c_addr);
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
    const unsigned *slots = context ? context : s_pair_slots;
    const app_cfg_t *c = &g_sensors[slots[sensor]].cfg_copy;
    return hal_gpio_capture_start(c->gpio_trig, c->gpio_echo,
                                  c->gpio_trig_us > 0 ? (uint32_t)c->gpio_trig_us : 10u,
                                  timeout_us);
}
static int pair_poll(void *context, unsigned sensor, hal_pulse_result_t *pulse)
{
    const unsigned *slots = context ? context : s_pair_slots;
    return hal_gpio_capture_poll(g_sensors[slots[sensor]].cfg_copy.gpio_echo, pulse);
}
static void pair_cancel(void *context, unsigned sensor)
{
    const unsigned *slots = context ? context : s_pair_slots;
    hal_gpio_capture_cancel(g_sensors[slots[sensor]].cfg_copy.gpio_echo);
}

int scheduler_pair_command(const char *args)
{
    if (!args) return HAL_ERR_INVALID;
    if (s_prev_running || pulse_sequence_active(&s_pair) || s_window.enabled) return HAL_ERR_BUSY;
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


static bool window_emit(const entrance_result_t *r, void *context)
{
    (void)context;
    event_record_t event = {0};
    if (!event_identity_next(&event.node_id, &event.boot_id, &event.source_seq)) {
        puts("[WINDOW] identity exhausted/unavailable: event refused");
        return false;
    }
    event.schema_version = EVENT_RECORD_SCHEMA_VERSION;
    event.event_id = event.source_seq;
    event.source_id = 0x57494e44u; /* one WINDOW service per provisioned node */
    event.window_start_us = r->start_us; event.window_end_us = r->end_us;
    event.quality_flags = r->quality_flags;
    event.rule_class = (uint8_t)r->classification;
    event.valid = 1;
    event.features[0] = (float)r->pairs; event.features[1] = (float)r->valid_pairs;
    event.features[2] = (float)r->failed_pairs; event.features[3] = (float)r->min_mm[0];
    event.features[4] = (float)r->min_mm[1]; event.features[5] = (float)r->max_pair_us;
    event.features[6] = (float)r->reason; event.features[7] = (float)r->skipped;
    bool queued = event_record_submit(&event);
    printf("[WINDOW] seq=%u class=%s reason=%u pairs=%u valid=%u failed=%u queued=%u\n",
           (unsigned)event.source_seq, event_class_name(r->classification), (unsigned)r->reason,
           r->pairs, r->valid_pairs, r->failed_pairs, queued ? 1u : 0u);
    return queued;
}

static size_t config_pins(const app_cfg_t *, int pins[4]);

int scheduler_window_command(const char *args)
{
    if (!args || strlen(args) >= 256) return HAL_ERR_INVALID;
    if (s_prev_running || s_window.enabled || pulse_sequence_active(&s_pair)) return HAL_ERR_BUSY;
    if (!event_identity_ready()) return HAL_ERR_IO;
    char text[256], *tokens[10], *save;
    strcpy(text, args);
    unsigned count = 0;
    char *token = strtok_r(text, " \t", &save);
    while (token && count < 10) { tokens[count++] = token; token = strtok_r(NULL, " \t", &save); }
    if (token || count != 10) return HAL_ERR_INVALID;
    int32_t values[7];
    for (unsigned i = 0; i < 7; ++i) {
        char *end;
        errno = 0;
        long long v = strtoll(tokens[3 + i], &end, 10);
        if (errno || *end || v < INT32_MIN || v > INT32_MAX) return HAL_ERR_INVALID;
        values[i] = (int32_t)v;
    }
    if (values[0] < 0 || values[1] <= 0 || values[1] > 4000 || values[2] <= 0 || values[2] > 4000 ||
        values[3] < -32767 || values[4] > 32767 || values[3] >= values[4] ||
        values[5] < -32767 || values[6] > 32767 || values[5] >= values[6]) return HAL_ERR_INVALID;
    unsigned slots[3] = {MAX_ACTIVE, MAX_ACTIVE, MAX_ACTIVE};
    for (unsigned i = 0; i < MAX_ACTIVE; ++i) if (g_sensors[i].in_use) {
        for (unsigned n = 0; n < 3; ++n) if (!strcmp(g_sensors[i].cfg_copy.name, tokens[n])) {
            if (slots[n] != MAX_ACTIVE) return HAL_ERR_INVALID;
            slots[n] = i;
        }
    }
    for (unsigned n = 0; n < 3; ++n) if (slots[n] == MAX_ACTIVE) return HAL_ERR_INVALID;
    if (slots[0] == slots[1] || slots[0] == slots[2] || slots[1] == slots[2]) return HAL_ERR_INVALID;
    entrance_config_t config = { .pair = {.first=0, .guard_us=(uint32_t)values[0],
        .window_us=PULSE_SEQUENCE_WINDOW_MAX_US, .error_policy=PULSE_SEQUENCE_COLLECT_BOTH},
        .occupied_mm={(uint32_t)values[1], (uint32_t)values[2]} };
    for (unsigned n = 0; n < 2; ++n) {
        const active_sensor_t *sensor = &g_sensors[slots[n]];
        if (!pulse_config(&sensor->cfg_copy) || sensor->pending) return HAL_ERR_INVALID;
        config.pair.timeout_us[n] = sensor->cfg_copy.gpio_pulse_timeout_us > 0 ?
            (uint32_t)sensor->cfg_copy.gpio_pulse_timeout_us : 30000u;
    }
    for (unsigned i = 0; i < MAX_ACTIVE; ++i) if (g_sensors[i].in_use && i != slots[0] && i != slots[1] &&
        pulse_config(&g_sensors[i].cfg_copy)) return HAL_ERR_BUSY; // no stray ultrasonic transmitter
    active_sensor_t *radar = &g_sensors[slots[2]];
    const app_cfg_t *c = &radar->cfg_copy;
    if (c->proto != proto_uart || !radar->bus->read_ready || !radar->bus->rx_dropped ||
        c->uart_baud != 256000 || c->uart_bits != 8 || c->uart_stop != 1 ||
        (c->uart_parity != 'N' && c->uart_parity != 'n') || c->uart_line_mode ||
        c->uart_pre_len || c->uart_post_delay_ms) return HAL_ERR_INVALID;
    for (unsigned i = 0; i < MAX_ACTIVE; ++i) {
        if (!g_sensors[i].in_use || i == slots[2]) continue;
        int pins[4];
        size_t n = config_pins(&g_sensors[i].cfg_copy, pins);
        for (size_t p = 0; p < n; ++p)
            if (pins[p] >= 0 && (pins[p] == c->uart_tx || pins[p] == c->uart_rx)) return HAL_ERR_BUSY;
    }
    const pulse_sequence_ops_t ops = {pair_start, pair_poll, pair_cancel};
    entrance_window_t candidate = {0};
    int rc = entrance_window_configure(&candidate, &config, &ops, s_window_slots, window_emit, NULL);
    if (rc != HAL_OK) return rc;
    uint32_t dropped = radar->bus->rx_dropped(radar->ctx);
    if (dropped == UINT32_MAX) return HAL_ERR_UNSUPPORTED;
    uint8_t discard[128];
    unsigned drained = 0;
    for (unsigned i = 0; i < 4; ++i) {
        int n = radar->bus->read_ready(radar->ctx, discard, sizeof discard);
        if (n < 0) return n;
        drained += (unsigned)n;
        if (n < (int)sizeof discard) break;
    }
    if (drained == 512) return HAL_ERR_BUSY; // retry after bounded stale-buffer drain
    memcpy(s_window_slots, slots, sizeof slots);
    s_zone = (ld2450_zone_t){values[3], values[4], values[5], values[6]};
    memset(&s_radar, 0, sizeof s_radar);
    s_radar_dropped = dropped;
    s_radar_dropped_start = dropped;
    s_radar_service_us = hal_time_us();
    s_window_warmup_until = s_radar_service_us + 60001u;
    s_window = candidate;
    puts("[WINDOW] configured; RUN enables radar acquisition, STOP/CLEAR cancels");
    return HAL_OK;
}

static void radar_report(unsigned targets, uint64_t now, void *context)
{
    (void)context;
    entrance_window_radar(&s_window, targets, now);
}
static void window_service(void)
{
    active_sensor_t *radar = &g_sensors[s_window_slots[2]];
    uint64_t now = hal_time_us();
    uint32_t dropped = radar->bus->rx_dropped(radar->ctx);
    uint8_t bytes[128];
    if (now < s_window_warmup_until) {
        (void)radar->bus->read_ready(radar->ctx, bytes, sizeof bytes);
        s_radar_service_us = now;
        return;
    }
    if (dropped != s_radar_dropped || now < s_radar_service_us ||
        now - s_radar_service_us >= ENTRANCE_RADAR_STALE_US) {
        s_radar_dropped = dropped;
        s_radar.used = 0;
        entrance_window_fault(&s_window);
        // Byte rings have no source timestamp. Discard old backlog after a
        // service gap/loss instead of renewing freshness with buffered frames.
        for (unsigned i = 0; i < 4; ++i)
            if (radar->bus->read_ready(radar->ctx, bytes, sizeof bytes) < (int)sizeof bytes) break;
        s_radar_service_us = now;
        entrance_window_step(&s_window, now);
        return;
    }
    s_radar_service_us = now;
    int n = radar->bus->read_ready(radar->ctx, bytes, sizeof bytes);
    dropped = radar->bus->rx_dropped(radar->ctx);
    if (dropped != s_radar_dropped) {
        s_radar_dropped = dropped;
        s_radar.used = 0;
        entrance_window_fault(&s_window);
        for (unsigned i = 0; i < 4; ++i)
            if (radar->bus->read_ready(radar->ctx, bytes, sizeof bytes) < (int)sizeof bytes) break;
        n = 0;
    }
    uint32_t bad = s_radar.malformed;
    if (n > 0) ld2450_feed(&s_radar, &s_zone, bytes, (size_t)n, hal_time_us(), radar_report, NULL);
    if (n < 0 || bad != s_radar.malformed) entrance_window_fault(&s_window);
    entrance_window_step(&s_window, hal_time_us());
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
            scheduler_window_stop();
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
    if (!running && s_prev_running) scheduler_window_stop();
    if (running && s_window.enabled) window_service();
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
        if (s_window.enabled && (i == (int)s_window_slots[0] || i == (int)s_window_slots[1] ||
                                 i == (int)s_window_slots[2])) continue;

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
        sensor_measurement_emit(cfg,NULL,HAL_ERR_TIMEOUT);
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
            sensor_measurement_emit(cfg,NULL,HAL_ERR_TIMEOUT);
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
                sensor_measurement_emit(cfg,NULL,n);
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
    if (s_window.enabled) {
        uint32_t window_wait = (entrance_window_wait_us(&s_window, now, 10000u) + 999u) / 1000u;
        if (window_wait < wait) wait = window_wait;
    }
    for (int i = 0; i < MAX_ACTIVE; i++) {
        const active_sensor_t *S = &g_sensors[i];
        if (!S->in_use || !S->bus || !S->cfg_copy.timing.enabled) continue;
        if (s_window.enabled && (i == (int)s_window_slots[0] || i == (int)s_window_slots[1] ||
                                 i == (int)s_window_slots[2])) continue;
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
