#include "application/entrance_window.h"
#include <string.h>

static void close_window(entrance_window_t *w, entrance_reason_t reason, uint64_t now)
{
    if (w->pair_pending) {
        uint64_t elapsed = now - w->pair.started_us;
        uint32_t measured = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
        if (measured > w->result.max_pair_us) w->result.max_pair_us = measured;
        ++w->result.failed_pairs; // cancelled/overdue attempts must not disappear from metrics
    }
    pulse_sequence_cancel(&w->pair, now);
    w->pair_pending = false;
    w->result.end_us = now;
    w->result.reason = reason;
    w->result.confirmed = w->confirmed;
    bool crossing = w->confirmed && !w->ambiguous && !w->faulty &&
                    reason == ENTRANCE_CROSSING && (w->stage == 7 || w->stage == 9);
    w->result.classification = crossing ? (w->stage == 7 ? EVENT_CLASS_ENTRY : EVENT_CLASS_EXIT) :
        reason == ENTRANCE_NO_CROSSING && !w->ambiguous && !w->faulty ? EVENT_CLASS_FALSE : EVENT_CLASS_INCOMPLETE;
    w->result.quality_flags |= EVENT_QUALITY_RULE_VALID;
    if (crossing) w->result.quality_flags |= EVENT_QUALITY_SEQUENCE_COMPLETE | EVENT_QUALITY_TIMING_VALID;
    if (w->faulty) w->result.quality_flags |= EVENT_QUALITY_SENSOR_FAULT;
    if (reason == ENTRANCE_RADAR_STALE) w->result.quality_flags |= EVENT_QUALITY_STALE;
    if (w->ambiguous || !w->confirmed || (!crossing && reason != ENTRANCE_NO_CROSSING))
        w->result.quality_flags |= EVENT_QUALITY_AMBIGUOUS;
    w->state = ENTRANCE_COOLDOWN; /* before callback: never offer twice */
    w->clear_pairs = 0;
    ++w->events;
    if (!w->emit(&w->result, w->emit_context)) ++w->drops;
}

int entrance_window_configure(entrance_window_t *w, const entrance_config_t *c,
                              const pulse_sequence_ops_t *ops, void *context,
                              entrance_emit_t emit, void *emit_context)
{
    if (!w || !c || !ops || !emit || !c->occupied_mm[0] || !c->occupied_mm[1]) return HAL_ERR_INVALID;
    if (w->enabled) return HAL_ERR_BUSY;
    pulse_sequence_t check = {0};
    int rc = pulse_sequence_begin(&check, &c->pair, ops, context, 1);
    if (rc != HAL_OK) return rc;
    entrance_config_t config = *c;
    pulse_sequence_ops_t callbacks = *ops;
    memset(w, 0, sizeof *w);
    w->config = config;
    w->config.pair.error_policy = PULSE_SEQUENCE_COLLECT_BOTH;
    w->ops = callbacks;
    w->capture_context = context;
    w->emit = emit;
    w->emit_context = emit_context;
    w->enabled = true;
    return HAL_OK;
}

void entrance_window_radar(entrance_window_t *w, unsigned targets, uint64_t now)
{
    if (!w || !w->enabled || targets > 3 || !now) return;
    if (now < w->last_us) {
        if (w->state == ENTRANCE_OPEN) close_window(w, ENTRANCE_CLOCK_ERROR, w->last_us);
        entrance_window_stop(w, w->last_us); return;
    }
    if (now > UINT64_MAX - ENTRANCE_WINDOW_US) { entrance_window_stop(w, now); return; }
    w->last_us = now;
    w->radar_seen = true;
    w->radar_us = now;
    w->radar_targets = targets;
    if (w->state == ENTRANCE_IDLE && targets) {
        memset(&w->result, 0, sizeof w->result);
        w->result.start_us = now;
        w->first_presence_us = w->next_pair_us = now;
        w->stage = w->clear_pairs = w->consecutive_failures = 0;
        w->clear_since_us = 0;
        w->confirmed = w->faulty = false;
        w->ambiguous = targets > 1;
        w->state = ENTRANCE_OPEN;
    } else if (w->state == ENTRANCE_OPEN && targets) {
        if (now - w->first_presence_us >= 50000u) w->confirmed = true;
        if (targets > 1) w->ambiguous = true;
    }
}

void entrance_window_fault(entrance_window_t *w)
{
    if (w && w->enabled && w->state == ENTRANCE_OPEN) w->faulty = true;
}

static void transition(entrance_window_t *w, unsigned mask)
{
    switch (w->stage) {
    case 0: if (!mask) w->stage = 1; else w->ambiguous = true; break;
    case 1:
        if (mask == 1) w->stage = 2;
        else if (mask == 2) w->stage = 5;
        else if (mask == 3) w->ambiguous = true;
        break;
    case 2: if (mask == 3) w->stage = 3; else if (mask != 1) w->ambiguous = true; break;
    case 3: if (mask == 2) w->stage = 4; else if (mask != 3) w->ambiguous = true; break;
    case 4: if (!mask) w->stage = 7; else if (mask != 2) w->ambiguous = true; break;
    case 5: if (mask == 3) w->stage = 6; else if (mask != 2) w->ambiguous = true; break;
    case 6: if (mask == 1) w->stage = 8; else if (mask != 3) w->ambiguous = true; break;
    case 8: if (!mask) w->stage = 9; else if (mask != 1) w->ambiguous = true; break;
    default: if (mask) w->ambiguous = true; break;
    }
}

static void consume_pair(entrance_window_t *w, uint64_t now)
{
    bool valid = w->pair.state == PULSE_SEQUENCE_COMPLETE && w->pair.outcome == PULSE_SEQUENCE_OK &&
                 w->pair.samples[0].valid && w->pair.samples[1].valid;
    for (unsigned i = 0; i < 2; ++i) {
        if (w->pair.samples[i].status == HAL_ERR_TIMEOUT && w->state == ENTRANCE_OPEN)
            w->result.quality_flags |= EVENT_QUALITY_SENSOR_TIMEOUT;
        if (w->pair.samples[i].pulse.width_us < 116u || w->pair.samples[i].pulse.width_us > 23200u)
            valid = false; // HC-SR04's specified 2 cm .. 4 m range
    }
    if (w->state == ENTRANCE_OPEN) {
        uint64_t elapsed = w->pair.finished_us - w->pair.started_us;
        if (elapsed > w->result.max_pair_us) w->result.max_pair_us = (uint32_t)elapsed;
    }
    if (!valid) {
        ++w->consecutive_failures;
        w->clear_pairs = 0;
        if (w->state == ENTRANCE_OPEN) {
            w->faulty = true;
            ++w->result.failed_pairs;
            if (w->consecutive_failures >= 3) close_window(w, ENTRANCE_SENSOR_FAULT, now);
        }
        return;
    }
    w->consecutive_failures = 0;
    unsigned mask = 0;
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t mm = (uint32_t)((uint64_t)w->pair.samples[i].pulse.width_us * 10u / 58u);
        if (mm <= w->config.occupied_mm[i]) mask |= 1u << i;
        if (w->state == ENTRANCE_OPEN && (!w->result.min_mm[i] || mm < w->result.min_mm[i]))
            w->result.min_mm[i] = mm;
    }
    if (!mask) {
        if (!w->clear_pairs) w->clear_since_us = now;
        ++w->clear_pairs;
    } else w->clear_pairs = 0;
    bool clear = w->clear_pairs >= 3 && now - w->clear_since_us >= ENTRANCE_CLEAR_US;
    if (w->state == ENTRANCE_OPEN) {
        ++w->result.valid_pairs;
        transition(w, mask);
        if (clear && (w->stage == 7 || w->stage == 9)) close_window(w, ENTRANCE_CROSSING, now);
        else if (clear && !w->radar_targets && w->confirmed)
            close_window(w, w->stage == 1 ? ENTRANCE_NO_CROSSING : ENTRANCE_DEADLINE, now);
    } else if (clear && !w->radar_targets && w->radar_seen && now - w->radar_us < ENTRANCE_RADAR_STALE_US) {
        w->state = ENTRANCE_IDLE;
    }
}

void entrance_window_step(entrance_window_t *w, uint64_t now)
{
    if (!w || !w->enabled) return;
    if (now < w->last_us || now > UINT64_MAX - ENTRANCE_WINDOW_US) {
        if (w->state == ENTRANCE_OPEN) close_window(w, ENTRANCE_CLOCK_ERROR, w->last_us);
        entrance_window_stop(w, w->last_us);
        return;
    }
    w->last_us = now;
    if (w->state == ENTRANCE_IDLE) return;
    if (w->state == ENTRANCE_OPEN) {
        if (now - w->radar_us >= ENTRANCE_RADAR_STALE_US) { close_window(w, ENTRANCE_RADAR_STALE, now); return; }
        if (!w->confirmed && now - w->first_presence_us >= ENTRANCE_RADAR_STALE_US) {
            close_window(w, ENTRANCE_UNCONFIRMED, now); return;
        }
        if (now - w->result.start_us >= ENTRANCE_WINDOW_US) { close_window(w, ENTRANCE_DEADLINE, now); return; }
    }
    if (w->pair_pending) {
        pulse_sequence_step(&w->pair, now);
        if (!pulse_sequence_active(&w->pair)) {
            w->pair_pending = false;
            consume_pair(w, now);
        }
    } else if (now >= w->next_pair_us) {
        if (w->state == ENTRANCE_OPEN && w->result.pairs >= ENTRANCE_MAX_PAIRS) return;
        // A variable first-capture duration shifts B's trigger within a pair.
        // A nominal 100 ms pair period alone cannot guarantee B's retrigger
        // interval. Skip a slot rather than fire either module within 60 ms.
        for (unsigned i = 0; i < 2; ++i) {
            if (w->pair.samples[i].attempted && now - w->pair.samples[i].started_us <= 60000u) {
                uint64_t missed = (now - w->next_pair_us) / ENTRANCE_PERIOD_US + 1;
                w->next_pair_us += missed * ENTRANCE_PERIOD_US;
                if (w->state == ENTRANCE_OPEN) { w->result.skipped += (uint32_t)missed; w->ambiguous = true; }
                return;
            }
        }
        uint64_t skipped = (now - w->next_pair_us) / ENTRANCE_PERIOD_US;
        if (skipped) w->ambiguous = true;
        if (w->state == ENTRANCE_OPEN) w->result.skipped += (uint32_t)skipped;
        w->next_pair_us += (skipped + 1) * ENTRANCE_PERIOD_US;
        int rc = pulse_sequence_begin(&w->pair, &w->config.pair, &w->ops, w->capture_context, now);
        if (rc != HAL_OK) { w->faulty = true; close_window(w, ENTRANCE_SENSOR_FAULT, now); return; }
        if (w->state == ENTRANCE_OPEN) ++w->result.pairs;
        w->pair_pending = true;
        pulse_sequence_step(&w->pair, now);
    }
}

void entrance_window_stop(entrance_window_t *w, uint64_t now)
{
    if (!w || !w->enabled) return;
    if (now < w->last_us) now = w->last_us;
    if (w->state == ENTRANCE_OPEN) close_window(w, ENTRANCE_CANCELLED, now);
    pulse_sequence_cancel(&w->pair, now);
    w->pair_pending = false;
    w->enabled = false;
}

uint32_t entrance_window_wait_us(const entrance_window_t *w, uint64_t now, uint32_t maximum)
{
    if (!w || !w->enabled || w->state == ENTRANCE_IDLE) return maximum;
    if (now < w->last_us) return 0;
    uint64_t target = w->next_pair_us;
    if (w->pair_pending) return pulse_sequence_wait_us(&w->pair, now, maximum);
    if (w->state == ENTRANCE_OPEN) {
        uint64_t stale = w->radar_us + ENTRANCE_RADAR_STALE_US;
        uint64_t end = w->result.start_us + ENTRANCE_WINDOW_US;
        if (stale < target) target = stale;
        if (end < target) target = end;
    }
    if (target <= now) return 0;
    return target - now < maximum ? (uint32_t)(target - now) : maximum;
}
