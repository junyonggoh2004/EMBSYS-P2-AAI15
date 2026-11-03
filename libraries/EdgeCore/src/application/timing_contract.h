#ifndef APPLICATION_TIMING_CONTRACT_H
#define APPLICATION_TIMING_CONTRACT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIMING_INTERVAL_MIN_MS 1u
#define TIMING_INTERVAL_MAX_MS 600000u
#define TIMING_TIMEOUT_MAX_MS 600000u

typedef enum {
    TIMING_MODE_PERIODIC = 0,
    TIMING_MODE_ONE_SHOT = 1,
    TIMING_MODE_EVENT_DRIVEN = 2
} timing_mode_t;

typedef enum {
    TIMING_STATUS_IDLE = 0,
    TIMING_STATUS_ARMED,
    TIMING_STATUS_DUE,
    TIMING_STATUS_COMPLETE,
    TIMING_STATUS_TIMEOUT,
    TIMING_STATUS_DISABLED
} timing_status_t;

typedef struct {
    timing_mode_t mode;
    uint32_t interval_ms;
    uint32_t timeout_ms;
    uint32_t window_ms;
    bool enabled;
} timing_config_t;

typedef struct {
    timing_config_t config;
    timing_status_t status;
    uint64_t armed_us;
    uint64_t due_us;
    uint64_t deadline_us;
    uint32_t executions;
    uint32_t timeouts;
} timing_state_t;

bool timing_config_validate(const timing_config_t *config, const char **reason);
void timing_state_init(timing_state_t *state, const timing_config_t *config,
                       uint64_t now_us, bool arm_now);
bool timing_state_trigger(timing_state_t *state, uint64_t now_us);
timing_status_t timing_state_poll(timing_state_t *state, uint64_t now_us);
/* Consume a due execution or timeout. Periodic state advances to a future
 * anchored slot, including after timeout; non-periodic state completes.
 * Deadlines cover lateness plus acquisition; completion at the deadline is
 * allowed. The earlier nonzero timeout/window bound applies. No driver is
 * preempted; the caller must suppress a sample that completes too late. */
void timing_state_complete(timing_state_t *state, uint64_t now_us);
uint32_t timing_state_wait_ms(const timing_state_t *state, uint64_t now_us,
                              uint32_t maximum_ms);
const char *timing_mode_name(timing_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif
