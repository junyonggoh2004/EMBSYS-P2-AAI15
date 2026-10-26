#ifndef EDGECORE_FLASH_D2_EVENT_LOG_H
#define EDGECORE_FLASH_D2_EVENT_LOG_H

#include <stddef.h>
#include <stdint.h>

#include "flash/w25q32.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Explicit wire format; never persist a C struct image. Each 80-byte record
 * occupies its own 128-byte slot, so a program cannot cross a flash page.
 * The caller supplies an approved, erased range. No erase API is provided. */
#define D2_LOG_WIRE_BYTES 80u
#define D2_LOG_SLOT_BYTES 128u
#define D2_LOG_VERSION 1u
#define D2_LOG_MAX_REGION_BYTES 4096u /* one explicitly approved sector */
#define D2_LOG_ERR_FULL (-10) /* approved, non-erasing log area exhausted */

typedef enum {
    D2_LOG_PUBLISH_NOT_ATTEMPTED = 0,
    D2_LOG_PUBLISH_ENQUEUED = 1,
    D2_LOG_PUBLISH_FAILED = 2
} d2_log_publish_t;

typedef enum {
    D2_LOG_APPLIED = 0,
    D2_LOG_NO_CHANGE = 1,
    D2_LOG_DUPLICATE = 2,
    D2_LOG_REORDERED = 3,
    D2_LOG_REJECTED = 4
} d2_log_result_t;

/* The adapter from D3/D6 is intentionally outside this module. node_id is a
 * stable per-node identity; boot_id must be unique across that node's boots;
 * log_seq increments for every processed event, including rejected events.
 * D3's event/source IDs may be zero for a rejected malformed input. */
typedef struct {
    uint64_t node_id;
    uint32_t boot_id;
    uint32_t log_seq;
    uint32_t event_id;
    uint32_t source_id;
    uint32_t source_seq;
    uint64_t window_start_us;
    uint64_t window_end_us;
    uint32_t quality_flags;
    uint32_t model_version;
    int32_t occupancy_before;
    int32_t occupancy_after;
    uint16_t confidence_bp; /* 0..10000, or UINT16_MAX when unavailable */
    uint8_t result_code;    /* d2_log_result_t */
    uint8_t decision_source; /* 0=none, 1=rule, 2=ML */
    uint8_t decided_class;   /* D3 event_class_t: 0..4 */
    uint8_t reason_code;     /* agreed D3/D6 numeric reason; 0=none */
    uint8_t event_valid;     /* D3 validity at processing, 0 or 1 */
    uint8_t degraded;        /* D6 degraded decision, 0 or 1 */
    uint8_t publish_status;  /* d2_log_publish_t, enqueue only */
} d2_log_record_t;

typedef struct {
    const w25q32_t *flash;
    uint32_t begin;
    uint32_t end;
    uint32_t next;
    uint32_t slots;
    uint32_t valid;
    uint32_t invalid; /* nonblank slots with invalid/torn records */
    uint32_t write_errors; /* current session; reset by d2_log_open */
} d2_event_log_t;

int d2_log_encode(const d2_log_record_t *record, uint8_t out[D2_LOG_WIRE_BYTES]);
int d2_log_decode(const uint8_t in[D2_LOG_WIRE_BYTES], d2_log_record_t *record);

/* Scans at most one approved 4 KiB sector without writing. Reboot recovery
 * skips torn or corrupt slots; append begins after the last nonblank slot. */
int d2_log_open(d2_event_log_t *log, const w25q32_t *flash,
                uint32_t approved_begin, uint32_t approved_end);
/* Exactly one blank slot is offered. A failed program attempt consumes that
 * slot conservatively, so no retry can overwrite a torn record. */
int d2_log_append(d2_event_log_t *log, const d2_log_record_t *record,
                  uint32_t deadline_ms);
int d2_log_read(const d2_event_log_t *log, uint32_t slot,
                d2_log_record_t *record);

#ifdef __cplusplus
}
#endif

#endif
