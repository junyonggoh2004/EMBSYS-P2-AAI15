#include "application/application_dispatch.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static application_state_t s_application;
static application_dispatch_stats_t s_stats;
static char s_payload[512];

void application_dispatch_init(void)
{
    application_state_init(&s_application, NULL);
    memset(&s_stats, 0, sizeof s_stats);
}

bool application_dispatch_handle(const application_message_t *message, size_t bytes,
                                  uint64_t now_us, application_publish_fn publish,
                                  void *context, application_dispatch_report_t *report)
{
    if (!report) return false;
    memset(report, 0, sizeof *report);
    bool valid = message && bytes == sizeof *message &&
                 message->kind >= APPLICATION_MESSAGE_EVENT &&
                 message->kind <= APPLICATION_MESSAGE_STATUS;
    if (!valid) {
        ++s_stats.malformed;
    } else if (message->kind == APPLICATION_MESSAGE_RESET) {
        application_state_init(&s_application, NULL);
        ++s_stats.resets;
    } else if (message->kind == APPLICATION_MESSAGE_EVENT) {
        const event_record_t *e = &message->event;
        report->result = application_process_event(&s_application, e, now_us);
        ++s_stats.processed;
        /* Preserve the earlier application topic's bounded result payload.
         * Every processed result is offered, including rejected/duplicate ones.
         * No publisher callback means no attempt, rather than delivery success. */
        int n = snprintf(s_payload, sizeof s_payload,
                         "{\"schema\":1,\"event_id\":%u,\"source_id\":%u,\"source_seq\":%u,"
                         "\"class\":\"%s\",\"result\":%u,\"occupancy\":%ld,\"degraded\":%s}",
                         (unsigned)e->event_id, (unsigned)e->source_id,
                         (unsigned)e->source_seq, event_class_name(report->result.decided_class),
                         (unsigned)report->result.code, (long)report->result.occupancy_after,
                         report->result.degraded ? "true" : "false");
        if (publish) {
            bool queued = n > 0 && (size_t)n < sizeof s_payload &&
                          publish(s_payload, (size_t)n, context);
            report->publish_status = queued ? APPLICATION_PUBLISH_ENQUEUED : APPLICATION_PUBLISH_FAILED;
            if (queued) ++s_stats.publish_enqueued;
            else ++s_stats.publish_failed;
        }
    }
    report->occupancy = s_application.occupancy;
    report->stats = s_stats;
    return valid;
}

static const char *skip_space(const char *s)
{
    while (isspace((unsigned char)*s)) ++s;
    return s;
}

static bool read_id(const char **cursor, uint32_t *id)
{
    const char *s = skip_space(*cursor);
    if (!isdigit((unsigned char)*s)) return false;
    errno = 0;
    char *end;
    unsigned long value = strtoul(s, &end, 10);
    if (errno == ERANGE || !value || value > UINT32_MAX ||
        !isspace((unsigned char)*end)) return false;
    *id = (uint32_t)value;
    *cursor = skip_space(end);
    return true;
}

bool application_command_parse(const char *line, uint64_t now_us,
                               application_message_t *message)
{
    if (!line || !message) return false;
    line = skip_space(line);
    if (strncasecmp(line, "APP", 3) || !isspace((unsigned char)line[3])) return false;
    const char *command = skip_space(line + 3);
    application_message_t parsed = { 0 };
    if (!strncasecmp(command, "RESET", 5) && !*skip_space(command + 5)) {
        parsed.kind = APPLICATION_MESSAGE_RESET;
    } else if (!strncasecmp(command, "STATUS", 6) && !*skip_space(command + 6)) {
        parsed.kind = APPLICATION_MESSAGE_STATUS;
    } else {
        if (strncasecmp(command, "EVENT", 5) || !isspace((unsigned char)command[5]) ||
            !now_us) return false;
        const char *cursor = command + 5;
        event_record_t *e = &parsed.event;
        if (!read_id(&cursor, &e->event_id) || !read_id(&cursor, &e->source_seq)) return false;
        const char *end = cursor;
        while (*end && !isspace((unsigned char)*end)) ++end;
        size_t n = (size_t)(end - cursor);
        if (*skip_space(end)) return false;
        event_class_t c;
        if (n == 5 && !strncasecmp(cursor, "entry", n)) c = EVENT_CLASS_ENTRY;
        else if (n == 4 && !strncasecmp(cursor, "exit", n)) c = EVENT_CLASS_EXIT;
        else if (n == 10 && !strncasecmp(cursor, "incomplete", n)) c = EVENT_CLASS_INCOMPLETE;
        else if (n == 5 && !strncasecmp(cursor, "false", n)) c = EVENT_CLASS_FALSE;
        else return false;
        parsed.kind = APPLICATION_MESSAGE_EVENT;
        e->schema_version = EVENT_RECORD_SCHEMA_VERSION;
        e->source_id = 1;  /* reserved by this synthetic console fixture */
        e->window_start_us = now_us > 1000 ? now_us - 1000 : 1;
        e->window_end_us = now_us;
        e->quality_flags = EVENT_QUALITY_SEQUENCE_COMPLETE | EVENT_QUALITY_TIMING_VALID |
                           EVENT_QUALITY_RULE_VALID;
        e->valid = 1;
        e->rule_class = (uint8_t)c;
    }
    *message = parsed;
    return true;
}
