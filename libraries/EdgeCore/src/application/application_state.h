#ifndef APPLICATION_STATE_H
#define APPLICATION_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "application/event_record.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APPLICATION_DEDUP_HISTORY 32u
#define APPLICATION_SOURCE_HISTORY 8u

typedef enum {
    APP_DECISION_NONE = 0,
    APP_DECISION_RULE,
    APP_DECISION_ML
} application_decision_source_t;

typedef enum {
    APP_RESULT_APPLIED = 0,
    APP_RESULT_NO_CHANGE,
    APP_RESULT_DUPLICATE,
    APP_RESULT_REORDERED,
    APP_RESULT_REJECTED
} application_result_code_t;

typedef struct {
    uint32_t freshness_ms;
    float ml_min_confidence;
    bool prefer_ml;
    bool accept_reordered;
} application_policy_t;

typedef struct {
    uint32_t source_id;
    uint32_t last_seq;
    uint64_t last_window_end_us;
} application_source_state_t;

/* Current local source-scoped key. Does not provide node/boot uniqueness or
 * persistence; those remain separate D3/D6 contracts. */
typedef struct {
    uint32_t source_id;
    uint32_t event_id;
} application_event_identity_t;

typedef struct {
    application_policy_t policy;
    int32_t occupancy;
    application_event_identity_t recent_events[APPLICATION_DEDUP_HISTORY];
    uint8_t recent_count;
    uint8_t recent_next;
    application_source_state_t sources[APPLICATION_SOURCE_HISTORY];
    uint32_t accepted;
    uint32_t rejected;
    uint32_t duplicates;
    uint32_t reordered;
} application_state_t;

typedef struct {
    application_result_code_t code;
    application_decision_source_t decision_source;
    event_class_t decided_class;
    int32_t occupancy_before;
    int32_t occupancy_after;
    bool degraded;
    const char *reason;
} application_result_t;

void application_state_init(application_state_t *state,
                            const application_policy_t *policy);
application_result_t application_process_event(application_state_t *state,
                                               const event_record_t *event,
                                               uint64_t now_us);

#ifdef __cplusplus
}
#endif

#endif
