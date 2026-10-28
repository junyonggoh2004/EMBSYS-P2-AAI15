#include "application/application_state.h"
#include <math.h>
#include <string.h>

static bool seen_event(const application_state_t *state, const event_record_t *event) {
    for (uint8_t i = 0; i < state->recent_count; i++)
        if (state->recent_events[i].source_id == event->source_id &&
            state->recent_events[i].event_id == event->event_id) return true;
    return false;
}

static void remember_event(application_state_t *state, const event_record_t *event) {
    state->recent_events[state->recent_next] =
        (application_event_identity_t){event->source_id, event->event_id};
    state->recent_next = (uint8_t)((state->recent_next + 1u) % APPLICATION_DEDUP_HISTORY);
    if (state->recent_count < APPLICATION_DEDUP_HISTORY) state->recent_count++;
}

static application_source_state_t *source_state(application_state_t *state, uint32_t source_id) {
    application_source_state_t *empty = NULL;
    for (uint8_t i = 0; i < APPLICATION_SOURCE_HISTORY; i++) {
        if (state->sources[i].source_id == source_id) return &state->sources[i];
        if (!state->sources[i].source_id && !empty) empty = &state->sources[i];
    }
    return empty;
}

void application_state_init(application_state_t *state,
                            const application_policy_t *policy) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->policy.freshness_ms = 5000u;
    state->policy.ml_min_confidence = 0.70f;
    state->policy.prefer_ml = true;
    state->policy.accept_reordered = false;
    if (policy) state->policy = *policy;
}

application_result_t application_process_event(application_state_t *state,
                                               const event_record_t *event,
                                               uint64_t now_us) {
    application_result_t out = { APP_RESULT_REJECTED, APP_DECISION_NONE,
                                 EVENT_CLASS_UNKNOWN, 0, 0, false, "state" };
    if (!state) return out;
    out.occupancy_before = state->occupancy;
    out.occupancy_after = state->occupancy;

    if (!isfinite(state->policy.ml_min_confidence) ||
        state->policy.ml_min_confidence < 0.0f || state->policy.ml_min_confidence > 1.0f) {
        state->rejected++;
        out.reason = "policy";
        out.degraded = true;
        return out;
    }

    const char *validation = NULL;
    if (!event_record_validate(event, now_us, state->policy.freshness_ms, &validation)) {
        state->rejected++;
        out.reason = validation;
        out.degraded = true;
        return out;
    }
    if (seen_event(state, event)) {
        state->duplicates++;
        out.code = APP_RESULT_DUPLICATE;
        out.reason = "duplicate";
        return out;
    }

    application_source_state_t *source = source_state(state, event->source_id);
    if (!source) {
        state->rejected++;
        out.reason = "source capacity";
        out.degraded = true;
        return out;
    }
    bool reordered = source && source->source_id &&
                     (event->source_seq <= source->last_seq ||
                      event->window_end_us < source->last_window_end_us);
    if (reordered && !state->policy.accept_reordered) {
        state->reordered++;
        out.code = APP_RESULT_REORDERED;
        out.reason = "reordered";
        out.degraded = true;
        remember_event(state, event);
        return out;
    }

    event_class_t decided = EVENT_CLASS_UNKNOWN;
    if (state->policy.prefer_ml && (event->quality_flags & EVENT_QUALITY_ML_VALID) &&
        event->ml_class != EVENT_CLASS_UNKNOWN &&
        event->ml_confidence >= state->policy.ml_min_confidence) {
        decided = (event_class_t)event->ml_class;
        out.decision_source = APP_DECISION_ML;
    } else if ((event->quality_flags & EVENT_QUALITY_RULE_VALID) &&
               event->rule_class != EVENT_CLASS_UNKNOWN) {
        decided = (event_class_t)event->rule_class;
        out.decision_source = APP_DECISION_RULE;
        out.degraded = (event->quality_flags & EVENT_QUALITY_ML_VALID) != 0u;
    } else {
        state->rejected++;
        out.reason = "no classification";
        out.degraded = true;
        remember_event(state, event);
        return out;
    }

    if ((decided == EVENT_CLASS_ENTRY || decided == EVENT_CLASS_EXIT) &&
        ((event->quality_flags & (EVENT_QUALITY_SEQUENCE_COMPLETE | EVENT_QUALITY_TIMING_VALID)) !=
         (EVENT_QUALITY_SEQUENCE_COMPLETE | EVENT_QUALITY_TIMING_VALID) ||
         (event->quality_flags & (EVENT_QUALITY_SENSOR_TIMEOUT | EVENT_QUALITY_AMBIGUOUS)))) {
        state->rejected++;
        out.decided_class = decided;
        out.reason = "quality";
        out.degraded = true;
        remember_event(state, event);
        return out;
    }

    if (decided == EVENT_CLASS_ENTRY && state->occupancy == INT32_MAX) {
        state->rejected++;
        out.decided_class = decided;
        out.reason = "occupancy capacity";
        out.degraded = true;
        remember_event(state, event);
        return out;
    }

    remember_event(state, event);
    if (source) {
        source->source_id = event->source_id;
        source->last_seq = event->source_seq;
        source->last_window_end_us = event->window_end_us;
    }
    out.decided_class = decided;
    out.reason = "accepted";
    state->accepted++;
    if (decided == EVENT_CLASS_ENTRY) {
        state->occupancy++;
        out.code = APP_RESULT_APPLIED;
    } else if (decided == EVENT_CLASS_EXIT && state->occupancy > 0) {
        state->occupancy--;
        out.code = APP_RESULT_APPLIED;
    } else {
        out.code = APP_RESULT_NO_CHANGE;
        if (decided == EVENT_CLASS_EXIT) out.reason = "occupancy floor";
    }
    out.occupancy_after = state->occupancy;
    return out;
}
