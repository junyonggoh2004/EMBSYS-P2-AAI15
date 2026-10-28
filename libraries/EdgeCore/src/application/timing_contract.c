#include "application/timing_contract.h"
#include <string.h>

bool timing_config_validate(const timing_config_t *config, const char **reason) {
    const char *why = NULL;
    if (!config) why = "null timing config";
    else if ((unsigned)config->mode > TIMING_MODE_EVENT_DRIVEN) why = "unsupported timing.mode";
    else if (config->enabled && config->mode == TIMING_MODE_PERIODIC &&
             (config->interval_ms < TIMING_INTERVAL_MIN_MS ||
              config->interval_ms > TIMING_INTERVAL_MAX_MS)) why = "timing.interval_ms out of range";
    else if (config->timeout_ms > TIMING_TIMEOUT_MAX_MS) why = "timing.timeout_ms out of range";
    else if (config->window_ms > TIMING_TIMEOUT_MAX_MS) why = "timing.window_ms out of range";
    else if (config->mode == TIMING_MODE_EVENT_DRIVEN && config->window_ms == 0u)
        why = "event timing requires timing.window_ms";
    else if (config->timeout_ms && config->window_ms && config->timeout_ms > config->window_ms)
        why = "timing.timeout_ms exceeds window";
    if (reason) *reason = why;
    return why == NULL;
}

static uint64_t execution_deadline(const timing_config_t *config, uint64_t due_us) {
    uint32_t limit = config->timeout_ms;
    if (config->mode != TIMING_MODE_PERIODIC && config->window_ms &&
        (!limit || config->window_ms < limit)) limit = config->window_ms;
    return limit ? due_us + (uint64_t)limit * 1000u : 0u;
}

void timing_state_init(timing_state_t *state, const timing_config_t *config,
                       uint64_t now_us, bool arm_now) {
    if (!state || !config) return;
    memset(state, 0, sizeof(*state));
    state->config = *config;
    if (!config->enabled || !timing_config_validate(config, NULL)) {
        state->status = TIMING_STATUS_DISABLED;
    } else if (config->mode == TIMING_MODE_PERIODIC || arm_now) {
        state->status = TIMING_STATUS_ARMED;
        state->armed_us = now_us;
        state->due_us = arm_now ? now_us : now_us + (uint64_t)config->interval_ms * 1000u;
        state->deadline_us = execution_deadline(config, state->due_us);
    } else {
        state->status = TIMING_STATUS_IDLE;
    }
}

bool timing_state_trigger(timing_state_t *state, uint64_t now_us) {
    if (!state || !state->config.enabled || state->status == TIMING_STATUS_DISABLED ||
        state->status == TIMING_STATUS_ARMED ||
        state->status == TIMING_STATUS_DUE) return false;
    state->armed_us = now_us;
    state->due_us = now_us;
    state->deadline_us = execution_deadline(&state->config, now_us);
    state->status = TIMING_STATUS_ARMED;
    return true;
}

timing_status_t timing_state_poll(timing_state_t *state, uint64_t now_us) {
    if (!state) return TIMING_STATUS_DISABLED;
    if ((state->status == TIMING_STATUS_ARMED || state->status == TIMING_STATUS_DUE) &&
        state->deadline_us && now_us > state->deadline_us) {
        state->status = TIMING_STATUS_TIMEOUT;
        state->timeouts++;
    } else if (state->status == TIMING_STATUS_ARMED && now_us >= state->due_us) {
        state->status = TIMING_STATUS_DUE;
    }
    return state->status;
}

void timing_state_complete(timing_state_t *state, uint64_t now_us) {
    if (!state || (state->status != TIMING_STATUS_DUE &&
                   state->status != TIMING_STATUS_TIMEOUT)) return;
    /* A synchronous driver cannot be cancelled here, but a late completion
       must not become a valid sample. A timeout is consumed exactly once. */
    timing_state_poll(state, now_us);
    if (state->status != TIMING_STATUS_TIMEOUT) state->executions++;
    if (state->config.mode == TIMING_MODE_PERIODIC) {
        uint64_t period_us = (uint64_t)state->config.interval_ms * 1000u;
        uint64_t next = state->due_us + period_us;
        if (next <= now_us) {
            uint64_t skipped = (now_us - next) / period_us + 1u;
            next += skipped * period_us;
        }
        state->due_us = next;
        state->armed_us = now_us;
        state->deadline_us = execution_deadline(&state->config, next);
        state->status = TIMING_STATUS_ARMED;
    } else {
        state->status = TIMING_STATUS_COMPLETE;
        state->deadline_us = 0u;
    }
}

uint32_t timing_state_wait_ms(const timing_state_t *state, uint64_t now_us,
                              uint32_t maximum_ms) {
    if (!state || state->status == TIMING_STATUS_DUE) return 0u;
    if (state->status != TIMING_STATUS_ARMED) return maximum_ms;
    uint64_t target = state->due_us;
    if (state->deadline_us && state->deadline_us < target) target = state->deadline_us;
    if (target <= now_us) return 0u;
    uint64_t wait = (target - now_us + 999u) / 1000u;
    return wait < maximum_ms ? (uint32_t)wait : maximum_ms;
}

const char *timing_mode_name(timing_mode_t mode) {
    switch (mode) {
    case TIMING_MODE_PERIODIC: return "periodic";
    case TIMING_MODE_ONE_SHOT: return "one_shot";
    case TIMING_MODE_EVENT_DRIVEN: return "event_driven";
    default: return "unknown";
    }
}
