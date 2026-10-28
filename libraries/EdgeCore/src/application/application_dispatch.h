#ifndef APPLICATION_DISPATCH_H
#define APPLICATION_DISPATCH_H

#include "application/application_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APPLICATION_MESSAGE_EVENT = 1,
    APPLICATION_MESSAGE_RESET,
    APPLICATION_MESSAGE_STATUS
} application_message_kind_t;

/* In-process FIFO item only; this is not the MQTT or flash wire format. */
typedef struct {
    uint32_t kind;
    event_record_t event;
} application_message_t;

typedef enum {
    APPLICATION_PUBLISH_NOT_ATTEMPTED = 0,
    APPLICATION_PUBLISH_ENQUEUED,
    APPLICATION_PUBLISH_FAILED
} application_publish_status_t;

typedef struct {
    uint32_t processed;
    uint32_t resets;
    uint32_t malformed;
    uint32_t publish_enqueued;
    uint32_t publish_failed;
} application_dispatch_stats_t;

typedef struct {
    application_result_t result;
    application_publish_status_t publish_status;
    application_dispatch_stats_t stats;
    int32_t occupancy;
} application_dispatch_report_t;

/* Must copy the payload before returning; true means enqueue, not delivery. */
typedef bool (*application_publish_fn)(const char *payload, size_t len, void *context);

/* Call once before producers/tasks start. All subsequent handle calls belong
 * to the single ApplicationTask consumer, including reset and status requests.
 * State and formatting BSS live in EdgeCore (PSRAM on the active S3 build).
 * Existing application_state policy is preserved; this does not settle the
 * outstanding identity, cross-node clock, or persistence contracts. */
void application_dispatch_init(void);
bool application_dispatch_handle(const application_message_t *message, size_t bytes,
                                  uint64_t now_us, application_publish_fn publish,
                                  void *context, application_dispatch_report_t *report);

/* Synthetic console fixture only. Strictly parses APP RESET, APP STATUS and
 * APP EVENT <nonzero-id> <nonzero-seq> <entry|exit|incomplete|false>.
 * Returns false without changing the output for any malformed command. */
bool application_command_parse(const char *line, uint64_t now_us,
                               application_message_t *message);

#ifdef __cplusplus
}
#endif
#endif
