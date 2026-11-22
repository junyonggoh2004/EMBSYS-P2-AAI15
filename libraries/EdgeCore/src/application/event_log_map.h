#ifndef APPLICATION_EVENT_LOG_MAP_H
#define APPLICATION_EVENT_LOG_MAP_H

#include <stdint.h>

#include "application/application_state.h"
#include "flash/d2_event_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/* D2-8: the numeric reason stored in a D2 log record for each reason
 * ApplicationTask gives. Any reason from event_record_validate() is stored as
 * EVENT_LOG_REASON_INVALID_EVENT. New reasons are appended, never renumbered. */
typedef enum {
    EVENT_LOG_REASON_NONE = 0,
    EVENT_LOG_REASON_ACCEPTED = 1,
    EVENT_LOG_REASON_POLICY = 2,
    EVENT_LOG_REASON_DUPLICATE = 3,
    EVENT_LOG_REASON_SOURCE_CAPACITY = 4,
    EVENT_LOG_REASON_REORDERED = 5,
    EVENT_LOG_REASON_NO_CLASSIFICATION = 6,
    EVENT_LOG_REASON_QUALITY = 7,
    EVENT_LOG_REASON_OCCUPANCY_CAPACITY = 8,
    EVENT_LOG_REASON_OCCUPANCY_FLOOR = 9,
    EVENT_LOG_REASON_INVALID_EVENT = 10
} event_log_reason_t;

uint8_t event_log_reason_code(const char *reason);
const char *event_log_reason_name(uint8_t code);

/* Builds the D2 record for one processed event. node_id and boot_id are the
 * logging node's own identity and log_seq counts every event it offers to the
 * log, so a record is unique even for synthetic events with no source node;
 * the event itself is identified by event_id, source_id and source_seq. The
 * confidence is stored only when the ML class decided the event. */
void event_log_map(const event_record_t *event, const application_result_t *result,
                   uint8_t publish_status, uint64_t node_id, uint32_t boot_id,
                   uint32_t log_seq, d2_log_record_t *out);

#ifdef __cplusplus
}
#endif

#endif
