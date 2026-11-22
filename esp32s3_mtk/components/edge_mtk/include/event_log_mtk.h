// event_log_mtk.h
// D2-8: the W25Q32 event log behind ApplicationTask (event_log_mtk.c).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "application/application_dispatch.h"
#include "flash/d2_event_log.h"

#ifdef __cplusplus
extern "C" {
#endif

// log_mbf capacity, in records
#define LOG_MBF_MSGS 8

// ApplicationTask, after the result is published: copies the record into
// log_mbf without waiting and wakes BenchmarkTask. Does nothing until LOG ON.
void event_log_offer(const event_record_t *event, const application_dispatch_report_t *report);

// BenchmarkTask: writes every record waiting in log_mbf and reads each back.
void event_log_flush(void);

// BenchmarkTask: prints one stored record (LOG READ <slot>).
void event_log_read_slot(uint32_t slot);

// ControlTask: LOG, LOG STATUS, LOG ON, LOG OFF, LOG READ <slot>, LOG FLUSH.
// Returns false if the line is not a LOG command.
bool event_log_command(const char *line);

#ifdef __cplusplus
}
#endif
