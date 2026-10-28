#include "application/event_record.h"
#include <math.h>

static event_record_sink_t s_sink;
static uint32_t s_next_event_id = 1u;

static bool valid_class(uint8_t value) {
    return value <= EVENT_CLASS_FALSE;
}

static bool finite_features(const event_record_t *event) {
    for (size_t i = 0; i < EVENT_FEATURE_COUNT; ++i)
        if (!isfinite(event->features[i])) return false;
    return true;
}

bool event_record_validate(const event_record_t *event, uint64_t now_us,
                           uint32_t freshness_ms, const char **reason) {
    const char *why = NULL;
    if (!event) why = "null";
    else if (event->schema_version != EVENT_RECORD_SCHEMA_VERSION) why = "schema";
    else if (!event->event_id || !event->source_id || !event->source_seq) why = "identity";
    else if (event->valid != 1u) why = "invalid";
    else if (event->reserved) why = "reserved";
    else if (!event->window_start_us || event->window_end_us < event->window_start_us) why = "window";
    else if (!valid_class(event->rule_class) || !valid_class(event->ml_class)) why = "class";
    else if (!isfinite(event->ml_confidence) || event->ml_confidence < 0.0f ||
             event->ml_confidence > 1.0f) why = "confidence";
    else if (!finite_features(event)) why = "features";
    else if (event->quality_flags & EVENT_QUALITY_STALE) why = "stale";
    else if (freshness_ms && now_us > event->window_end_us &&
             now_us - event->window_end_us > (uint64_t)freshness_ms * 1000u) why = "stale";
    if (reason) *reason = why;
    return why == NULL;
}

const char *event_class_name(event_class_t event_class) {
    switch (event_class) {
    case EVENT_CLASS_ENTRY: return "entry";
    case EVENT_CLASS_EXIT: return "exit";
    case EVENT_CLASS_INCOMPLETE: return "incomplete";
    case EVENT_CLASS_FALSE: return "false";
    default: return "unknown";
    }
}

void event_record_set_sink(event_record_sink_t sink) {
    s_sink = sink;
}

bool event_record_submit(const event_record_t *event) {
    return s_sink && event && s_sink(event);
}

uint32_t event_source_id(const char *source) {
    /* FNV-1a gives a stable, allocation-free identifier for the configured
       source name. Zero is reserved for invalid/unset. */
    uint32_t hash = 2166136261u;
    if (!source || !*source) return 0u;
    while (*source) {
        hash ^= (uint8_t)*source++;
        hash *= 16777619u;
    }
    return hash ? hash : 1u;
}

uint32_t event_record_next_id(void) {
    uint32_t id = s_next_event_id++;
    if (!id) id = s_next_event_id++;
    return id;
}
