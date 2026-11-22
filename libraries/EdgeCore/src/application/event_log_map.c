#include "application/event_log_map.h"

#include <math.h>
#include <string.h>

static const char *const k_reasons[] = {
    [EVENT_LOG_REASON_NONE] = "-",
    [EVENT_LOG_REASON_ACCEPTED] = "accepted",
    [EVENT_LOG_REASON_POLICY] = "policy",
    [EVENT_LOG_REASON_DUPLICATE] = "duplicate",
    [EVENT_LOG_REASON_SOURCE_CAPACITY] = "source capacity",
    [EVENT_LOG_REASON_REORDERED] = "reordered",
    [EVENT_LOG_REASON_NO_CLASSIFICATION] = "no classification",
    [EVENT_LOG_REASON_QUALITY] = "quality",
    [EVENT_LOG_REASON_OCCUPANCY_CAPACITY] = "occupancy capacity",
    [EVENT_LOG_REASON_OCCUPANCY_FLOOR] = "occupancy floor",
    [EVENT_LOG_REASON_INVALID_EVENT] = "invalid event",
};

uint8_t event_log_reason_code(const char *reason)
{
    if (!reason || !*reason) return EVENT_LOG_REASON_NONE;
    for (uint8_t i = EVENT_LOG_REASON_ACCEPTED; i < EVENT_LOG_REASON_INVALID_EVENT; ++i)
        if (!strcmp(reason, k_reasons[i])) return i;
    return EVENT_LOG_REASON_INVALID_EVENT;
}

const char *event_log_reason_name(uint8_t code)
{
    return code < sizeof k_reasons / sizeof k_reasons[0] ? k_reasons[code] : "unknown";
}

void event_log_map(const event_record_t *event, const application_result_t *result,
                   uint8_t publish_status, uint64_t node_id, uint32_t boot_id,
                   uint32_t log_seq, d2_log_record_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->node_id = node_id;
    out->boot_id = boot_id;
    out->log_seq = log_seq;
    out->confidence_bp = UINT16_MAX;
    out->result_code = D2_LOG_REJECTED;
    out->publish_status = publish_status;
    if (event) {
        out->event_id = event->event_id;
        out->source_id = event->source_id;
        out->source_seq = event->source_seq;
        out->window_start_us = event->window_start_us;
        out->window_end_us = event->window_end_us;
        out->quality_flags = event->quality_flags;
        out->model_version = event->model_version;
        out->event_valid = event->valid ? 1u : 0u;
    }
    if (!result) return;
    out->occupancy_before = result->occupancy_before;
    out->occupancy_after = result->occupancy_after;
    out->result_code = (uint8_t)result->code;
    out->decision_source = (uint8_t)result->decision_source;
    out->decided_class = (uint8_t)result->decided_class;
    out->reason_code = event_log_reason_code(result->reason);
    out->degraded = result->degraded ? 1u : 0u;
    if (event && result->decision_source == APP_DECISION_ML &&
        isfinite(event->ml_confidence) && event->ml_confidence >= 0.0f &&
        event->ml_confidence <= 1.0f)
        out->confidence_bp = (uint16_t)(event->ml_confidence * 10000.0f + 0.5f);
}
