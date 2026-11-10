#ifndef APPLICATION_ENTRANCE_WINDOW_H
#define APPLICATION_ENTRANCE_WINDOW_H
#include "application/pulse_sequence.h"
#include "application/event_record.h"

#define ENTRANCE_PERIOD_US 100000u
#define ENTRANCE_WINDOW_US 2000000u
#define ENTRANCE_CLEAR_US 200000u
#define ENTRANCE_RADAR_STALE_US 300000u
#define ENTRANCE_MAX_PAIRS 20u
#define ENTRANCE_TIMING_BINS 51u /* <=1 ms .. <=50 ms, then overflow */

typedef enum { ENTRANCE_IDLE, ENTRANCE_OPEN, ENTRANCE_COOLDOWN } entrance_state_t;
typedef enum {
    ENTRANCE_CROSSING, ENTRANCE_NO_CROSSING, ENTRANCE_DEADLINE,
    ENTRANCE_SENSOR_FAULT, ENTRANCE_RADAR_STALE, ENTRANCE_UNCONFIRMED,
    ENTRANCE_CANCELLED, ENTRANCE_CLOCK_ERROR, ENTRANCE_INCOMPLETE
} entrance_reason_t;
typedef struct {
    uint64_t start_us, end_us;
    event_class_t classification;
    uint32_t quality_flags;
    entrance_reason_t reason;
    unsigned pairs, valid_pairs, failed_pairs, skipped;
    uint32_t min_mm[2], max_pair_us;
    bool confirmed;
} entrance_result_t;
typedef bool (*entrance_emit_t)(const entrance_result_t *, void *);
typedef struct {
    pulse_sequence_config_t pair;
    uint32_t occupied_mm[2]; /* caller supplies physical thresholds */
} entrance_config_t;
typedef struct {
    uint32_t pairs, valid, failed, cancelled, deadline_misses, skipped;
    uint32_t min_us, max_us;
    uint64_t sum_us;
    uint32_t histogram[ENTRANCE_TIMING_BINS];
    uint32_t entry, exit, incomplete, false_events, window_deadlines, radar_stale, window_faults;
} entrance_metrics_t;
typedef struct {
    entrance_config_t config;
    pulse_sequence_ops_t ops;
    void *capture_context, *emit_context;
    entrance_emit_t emit;
    pulse_sequence_t pair;
    entrance_result_t result;
    entrance_metrics_t metrics; /* cumulative until configure/CLEAR, PSRAM in EdgeCore */
    entrance_state_t state;
    uint64_t last_us, radar_us, first_presence_us, next_pair_us, clear_since_us;
    unsigned radar_targets, stage, clear_pairs, consecutive_failures;
    uint32_t events, drops;
    bool enabled, radar_seen, confirmed, pair_pending, ambiguous, faulty;
} entrance_window_t;

/* Zero-initialize once. Configure only while disabled; no capture is armed. */
int entrance_window_configure(entrance_window_t *, const entrance_config_t *,
                              const pulse_sequence_ops_t *, void *, entrance_emit_t, void *);
/* Only valid, fully decoded radar frames call this. Invalid bytes never renew
 * freshness. Count is the number of targets inside the configured zone. */
void entrance_window_radar(entrance_window_t *, unsigned targets, uint64_t now_us);
void entrance_window_step(entrance_window_t *, uint64_t now_us);
void entrance_window_stop(entrance_window_t *, uint64_t now_us);
void entrance_window_fault(entrance_window_t *); /* UART loss / parser corruption */
uint32_t entrance_window_wait_us(const entrance_window_t *, uint64_t now_us, uint32_t max_us);
/* Nearest-rank percentile upper bound in 1 ms buckets. UINT32_MAX means
 * >50 ms; zero means no samples or an invalid percentile. Includes failed
 * and cancelled attempts, including cooldown captures. Never resets counters. */
uint32_t entrance_window_percentile_us(const entrance_window_t *, unsigned percentile);
#endif
