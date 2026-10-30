#include "application/pulse_sequence.h"
#include <string.h>

bool pulse_sequence_active(const pulse_sequence_t *s)
{
    return s && (s->state == PULSE_SEQUENCE_ARM || s->state == PULSE_SEQUENCE_WAIT ||
                 s->state == PULSE_SEQUENCE_GUARD);
}

int pulse_sequence_begin(pulse_sequence_t *s, const pulse_sequence_config_t *c,
                         const pulse_sequence_ops_t *ops, void *context, uint64_t now)
{
    if (!s || !c || !ops || !ops->start || !ops->poll || !ops->cancel)
        return HAL_ERR_INVALID;
    if (pulse_sequence_active(s)) return HAL_ERR_BUSY;
    uint64_t budget = (uint64_t)c->timeout_us[0] + c->guard_us + c->timeout_us[1];
    if (c->first > 1 || !c->timeout_us[0] || !c->timeout_us[1] ||
        !c->window_us || c->window_us > PULSE_SEQUENCE_WINDOW_MAX_US || budget > c->window_us ||
        (c->error_policy != PULSE_SEQUENCE_FAIL_FAST &&
         c->error_policy != PULSE_SEQUENCE_COLLECT_BOTH) ||
        now > UINT64_MAX - c->window_us) return HAL_ERR_INVALID;
    /* Copy before clearing: callers may reuse the previous config/ops. */
    pulse_sequence_config_t config = *c;
    pulse_sequence_ops_t callbacks = *ops;
    memset(s, 0, sizeof *s);
    s->config = config;
    s->ops = callbacks;
    s->context = context;
    s->state = PULSE_SEQUENCE_ARM;
    s->samples[0].status = s->samples[1].status = PULSE_SEQUENCE_NOT_ATTEMPTED;
    s->current = config.first;
    s->started_us = s->last_us = now;
    return HAL_OK;
}

static void release(pulse_sequence_t *s)
{
    if (!s->owns_capture) return;
    s->ops.cancel(s->context, s->current);
    s->owns_capture = false;
}

static void finish(pulse_sequence_t *s, pulse_sequence_state_t state,
                    pulse_sequence_outcome_t outcome, uint64_t now)
{
    if (s->owns_capture) {
        pulse_sequence_sample_t *sample = &s->samples[s->current];
        sample->status = outcome == PULSE_SEQUENCE_USER_CANCELLED ? PULSE_SEQUENCE_SAMPLE_CANCELLED :
                         outcome == PULSE_SEQUENCE_WINDOW_EXPIRED ? HAL_ERR_TIMEOUT : HAL_ERR_INVALID;
        sample->consumed_us = now;
    }
    release(s);
    s->state = state;
    s->outcome = outcome;
    s->finished_us = now;
}

static void consume(pulse_sequence_t *s, int status, uint64_t now)
{
    pulse_sequence_sample_t *sample = &s->samples[s->current];
    release(s);
    sample->status = status;
    sample->consumed_us = now;
    sample->valid = status == HAL_OK;
    if (status != HAL_OK) {
        memset(&sample->pulse, 0, sizeof sample->pulse);
        s->outcome = PULSE_SEQUENCE_CAPTURE_ERROR;
    }
    ++s->completed;
    if (status != HAL_OK && s->config.error_policy == PULSE_SEQUENCE_FAIL_FAST) {
        finish(s, PULSE_SEQUENCE_FAILED, PULSE_SEQUENCE_CAPTURE_ERROR, now);
    } else if (s->completed == 2) {
        finish(s, PULSE_SEQUENCE_COMPLETE, s->outcome, now);
    } else {
        s->current = 1u - s->current;
        uint64_t remaining = s->started_us + s->config.window_us - now;
        if ((uint64_t)s->config.guard_us + s->config.timeout_us[s->current] > remaining) {
            finish(s, PULSE_SEQUENCE_FAILED, PULSE_SEQUENCE_WINDOW_EXPIRED, now);
            return;
        }
        // Conservative guard begins when the first outcome is consumed,
        // including timeout/error. Late service never shortens the guard.
        s->guard_until_us = now + s->config.guard_us;
        s->state = PULSE_SEQUENCE_GUARD;
    }
}

pulse_sequence_state_t pulse_sequence_step(pulse_sequence_t *s, uint64_t now)
{
    if (!s) return PULSE_SEQUENCE_IDLE;
    if (!pulse_sequence_active(s)) return s->state;
    if (now < s->last_us) {
        finish(s, PULSE_SEQUENCE_FAILED, PULSE_SEQUENCE_CLOCK_ERROR, s->last_us);
        return s->state;
    }
    s->last_us = now;
    if (now > s->started_us + s->config.window_us) {
        finish(s, PULSE_SEQUENCE_FAILED, PULSE_SEQUENCE_WINDOW_EXPIRED, now);
        return s->state;
    }
    if (s->state == PULSE_SEQUENCE_GUARD) {
        if (now < s->guard_until_us) return s->state;
        s->state = PULSE_SEQUENCE_ARM;
    }
    pulse_sequence_sample_t *sample = &s->samples[s->current];
    if (s->state == PULSE_SEQUENCE_ARM) {
        // Refuse a late start that cannot fit its full capture allowance.
        uint64_t remaining = s->started_us + s->config.window_us - now;
        if (remaining < s->config.timeout_us[s->current]) {
            finish(s, PULSE_SEQUENCE_FAILED, PULSE_SEQUENCE_WINDOW_EXPIRED, now);
            return s->state;
        }
        sample->attempted = true;
        sample->status = HAL_PULSE_PENDING;
        sample->started_us = now;
        int rc = s->ops.start(s->context, s->current, s->config.timeout_us[s->current]);
        if (rc == HAL_OK) {
            s->owns_capture = true;
            s->state = PULSE_SEQUENCE_WAIT;
        } else {
            consume(s, rc < 0 ? rc : HAL_ERR_IO, now);
        }
        return s->state;
    }
    uint64_t deadline = sample->started_us + s->config.timeout_us[s->current];
    if (now > deadline) {
        consume(s, HAL_ERR_TIMEOUT, now);
        return s->state;
    }
    hal_pulse_result_t pulse = {0};
    int rc = s->ops.poll(s->context, s->current, &pulse);
    if (rc == HAL_PULSE_PENDING) {
        if (now == deadline) consume(s, HAL_ERR_TIMEOUT, now);
        return s->state;
    }
    if (rc == HAL_OK) {
        // Never promote malformed, future, stale or late callback data to
        // valid observation quality. Keep sensor timestamps, not arrival order.
        if (pulse.rise_us < sample->started_us || pulse.fall_us <= pulse.rise_us ||
            pulse.fall_us > now || pulse.fall_us > deadline ||
            pulse.fall_us - pulse.rise_us != pulse.width_us) rc = HAL_ERR_IO;
        else sample->pulse = pulse;
    }
    consume(s, rc <= HAL_OK ? rc : HAL_ERR_IO, now);
    return s->state;
}

void pulse_sequence_cancel(pulse_sequence_t *s, uint64_t now)
{
    if (!pulse_sequence_active(s)) return;
    if (now < s->last_us) now = s->last_us;
    finish(s, PULSE_SEQUENCE_CANCELLED, PULSE_SEQUENCE_USER_CANCELLED, now);
}

uint32_t pulse_sequence_wait_us(const pulse_sequence_t *s, uint64_t now, uint32_t max_us)
{
    if (!pulse_sequence_active(s)) return max_us;
    if (now < s->last_us || s->state == PULSE_SEQUENCE_ARM) return 0;
    uint64_t target = s->started_us + s->config.window_us;
    if (s->state == PULSE_SEQUENCE_GUARD && s->guard_until_us < target)
        target = s->guard_until_us;
    if (s->state == PULSE_SEQUENCE_WAIT) {
        uint64_t deadline = s->samples[s->current].started_us + s->config.timeout_us[s->current];
        if (deadline < target) target = deadline;
        if (max_us > 1000) max_us = 1000;
    }
    if (target <= now) return 0;
    uint64_t remaining = target - now;
    return remaining < max_us ? (uint32_t)remaining : max_us;
}
