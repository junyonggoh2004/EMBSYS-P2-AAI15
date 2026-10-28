#ifndef APPLICATION_EVENT_RECORD_H
#define APPLICATION_EVENT_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_RECORD_SCHEMA_VERSION 1u
#define EVENT_FEATURE_COUNT 8u

typedef enum {
    EVENT_CLASS_UNKNOWN = 0,
    EVENT_CLASS_ENTRY = 1,
    EVENT_CLASS_EXIT = 2,
    EVENT_CLASS_INCOMPLETE = 3,
    EVENT_CLASS_FALSE = 4
} event_class_t;

typedef enum {
    EVENT_QUALITY_NONE = 0,
    EVENT_QUALITY_SEQUENCE_COMPLETE = 1u << 0,
    EVENT_QUALITY_TIMING_VALID = 1u << 1,
    EVENT_QUALITY_SENSOR_TIMEOUT = 1u << 2,
    EVENT_QUALITY_STALE = 1u << 3,
    EVENT_QUALITY_AMBIGUOUS = 1u << 4,
    EVENT_QUALITY_ML_VALID = 1u << 5,
    EVENT_QUALITY_RULE_VALID = 1u << 6
} event_quality_flag_t;

/* Fixed-size, pointer-free application contract shared by scheduler,
 * inference, ApplicationTask and evidence tooling. */
typedef struct {
    uint32_t schema_version;
    uint32_t event_id;
    uint32_t source_id;
    uint32_t source_seq;
    uint64_t window_start_us;
    uint64_t window_end_us;
    float features[EVENT_FEATURE_COUNT];
    uint32_t quality_flags;
    uint32_t model_version;
    float ml_confidence;
    uint8_t valid;
    uint8_t rule_class;
    uint8_t ml_class;
    uint8_t reserved;
} event_record_t;

/* Requires finite confidence/features, valid==1, reserved==0 and no STALE
 * flag. Freshness arithmetic still assumes a common monotonic time basis;
 * a cross-node clock/identity contract must be agreed before remote use. */
bool event_record_validate(const event_record_t *event, uint64_t now_us,
                           uint32_t freshness_ms, const char **reason);
const char *event_class_name(event_class_t event_class);

typedef bool (*event_record_sink_t)(const event_record_t *event);
void event_record_set_sink(event_record_sink_t sink);
bool event_record_submit(const event_record_t *event);
uint32_t event_source_id(const char *source);
uint32_t event_record_next_id(void);

#ifdef __cplusplus
}
#endif

#endif
