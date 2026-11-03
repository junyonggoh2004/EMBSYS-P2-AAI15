#ifndef APPLICATION_PULSE_SEQUENCE_H
#define APPLICATION_PULSE_SEQUENCE_H

#include "hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One finite, two-sensor acquisition. All calls belong to one scheduler
 * owner. The caller supplies every physical timing value and maps A/B to
 * configured capture resources. This module defines no entrance classifier,
 * radar policy or event identity. D3's user-confirmed 50 ms limit applies to
 * the complete pair, including both captures and the acoustic guard. */
#define PULSE_SEQUENCE_WINDOW_MAX_US 50000u
#define PULSE_SEQUENCE_NOT_ATTEMPTED 2
#define PULSE_SEQUENCE_SAMPLE_CANCELLED 3
typedef enum {
    PULSE_SEQUENCE_FAIL_FAST = 0,
    PULSE_SEQUENCE_COLLECT_BOTH = 1
} pulse_sequence_error_policy_t;

typedef struct {
    unsigned first;                 /* 0 = A, 1 = B */
    uint32_t timeout_us[2];          /* whole capture, indexed by A/B */
    uint32_t guard_us;               /* after first outcome is consumed */
    uint32_t window_us;              /* whole sequence */
    pulse_sequence_error_policy_t error_policy;
} pulse_sequence_config_t;

typedef struct {
    /* start/poll/cancel must be nonblocking except the bounded trigger.
     * start: HAL_OK owns the capture; any error owns nothing.
     * poll: HAL_OK supplies edge timestamps/width, HAL_PULSE_PENDING keeps
     * ownership, negative HAL_ERR_* is terminal. cancel is idempotent.
     * Keep callbacks/context valid until the sequence is terminal. */
    int (*start)(void *context, unsigned sensor, uint32_t timeout_us);
    int (*poll)(void *context, unsigned sensor, hal_pulse_result_t *pulse);
    void (*cancel)(void *context, unsigned sensor);
} pulse_sequence_ops_t;

typedef enum {
    PULSE_SEQUENCE_IDLE = 0,
    PULSE_SEQUENCE_ARM,
    PULSE_SEQUENCE_WAIT,
    PULSE_SEQUENCE_GUARD,
    PULSE_SEQUENCE_COMPLETE,
    PULSE_SEQUENCE_FAILED,
    PULSE_SEQUENCE_CANCELLED
} pulse_sequence_state_t;

typedef enum {
    PULSE_SEQUENCE_OK = 0,
    PULSE_SEQUENCE_CAPTURE_ERROR,
    PULSE_SEQUENCE_WINDOW_EXPIRED,
    PULSE_SEQUENCE_CLOCK_ERROR,
    PULSE_SEQUENCE_USER_CANCELLED
} pulse_sequence_outcome_t;

typedef struct {
    bool attempted, valid;
    int status;                    /* HAL_OK/PENDING/ERR_*, or constants above */
    uint64_t started_us, consumed_us;
    hal_pulse_result_t pulse;       /* zero unless validated complete */
} pulse_sequence_sample_t;

typedef struct {
    pulse_sequence_state_t state;
    pulse_sequence_outcome_t outcome;
    pulse_sequence_config_t config;
    pulse_sequence_ops_t ops;
    void *context;
    pulse_sequence_sample_t samples[2]; /* always indexed A/B, not order */
    uint64_t started_us, finished_us, last_us, guard_until_us;
    unsigned current, completed;
    bool owns_capture;
} pulse_sequence_t;

/* Zero-initialize once before use. A busy or invalid begin leaves all state
 * unchanged and touches no hardware. No implicit restart or endless loop. */
int pulse_sequence_begin(pulse_sequence_t *sequence,
                         const pulse_sequence_config_t *config,
                         const pulse_sequence_ops_t *ops, void *context,
                         uint64_t now_us);
/* Performs at most one start or poll; no sleeps, allocation, queues or flash.
 * Missing first/second edges consume the configured capture deadline. A
 * fail-fast error stops; collect-both permits the other capture after guard.
 * COMPLETE with CAPTURE_ERROR is a degraded observation, not two valid pulses.
 * Delayed service cannot extend a capture/sequence deadline. */
pulse_sequence_state_t pulse_sequence_step(pulse_sequence_t *sequence, uint64_t now_us);
void pulse_sequence_cancel(pulse_sequence_t *sequence, uint64_t now_us);
bool pulse_sequence_active(const pulse_sequence_t *sequence);
/* Requested maximum sleep before another step (us). Pending capture requests
 * at most 1000 us; physical service timing still needs target measurement. */
uint32_t pulse_sequence_wait_us(const pulse_sequence_t *sequence,
                               uint64_t now_us, uint32_t maximum_us);

#ifdef __cplusplus
}
#endif
#endif
