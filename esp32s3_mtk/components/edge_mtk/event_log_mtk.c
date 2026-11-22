// event_log_mtk.c
// D2-8: every event ApplicationTask processes is offered to the W25Q32 event
// log after its result has been published. ApplicationTask only copies the
// 80-byte record into log_mbf without waiting (8 records; when it is full the
// newest record is dropped and counted, as for every other buffer) and wakes
// BenchmarkTask. BenchmarkTask, the lowest-priority task and the one that
// already runs the SPI flash commands, writes each record into the next blank
// slot, reads it back and compares it. A missing, full or failing flash module
// is reported on the console and nothing else depends on it: occupancy, the
// published result and sampling are already done before the record is written.
//
// The log area is the approved sector 0x3FF000-0x3FFFFF above the 256-byte
// SPI test page: 30 slots of 128 bytes from 0x3FF100. It is never erased, so
// logging is off at boot and armed with LOG ON; SPILOGWRITE's fixture record,
// if present, stays in slot 0 and the log continues after it.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <tk/tkernel.h>
#include "application/event_identity.h"
#include "application/event_log_map.h"
#include "flash/d2_event_log.h"
#include "flash/w25q32.h"
#include "hal/hal.h"
#include "event_log_mtk.h"
#include "runtime_mtk.h"

#define LOG_PART_ID     0xef4016u
#define LOG_BEGIN       0x3ff100u
#define LOG_END         0x400000u
#define LOG_SLOTS       ((LOG_END - LOG_BEGIN) / D2_LOG_SLOT_BYTES)
#define LOG_DEADLINE_MS 200u

static volatile bool s_enabled;
static uint32_t s_seq;                 // ApplicationTask only
static volatile uint32_t s_offered, s_dropped, s_written, s_failed, s_full, s_max_us;
static volatile int s_last_error;

static void count(volatile uint32_t *c) { __atomic_add_fetch(c, 1, __ATOMIC_RELAXED); }

void event_log_offer(const event_record_t *event, const application_dispatch_report_t *report)
{
    if (!s_enabled || !event || !report) return;
    uint64_t node = 0;
    uint32_t boot = 0;
    (void)event_identity_get(&node, &boot);   // without an identity the write reports the invalid record
    d2_log_record_t record;
    event_log_map(event, &report->result, (uint8_t)report->publish_status, node, boot, ++s_seq, &record);
    count(&s_offered);
    if (!runtime_send(log_mbf, &record, sizeof record)) {
        count(&s_dropped);
        printf("[LOG] seq=%u event=%u dropped: log queue full\n",
               (unsigned)record.log_seq, (unsigned)record.event_id);
        return;
    }
    bench_msg_t wake = { .name = "LOGFLUSH" };
    (void)runtime_send(bench_mbf, &wake, sizeof wake);   // if full, the next wake-up writes it
}

static int log_open(w25q32_t *flash, d2_event_log_t *log, int *device)
{
    *device = hal_spi_open("fspi", 11, 12, 13, 10, 1000000, 0);
    if (*device < 0) return *device;
    int rc = w25q32_probe(flash, *device, LOG_PART_ID);
    if (rc == HAL_OK) rc = d2_log_open(log, flash, LOG_BEGIN, LOG_END);
    if (rc != HAL_OK) {
        (void)hal_spi_close(*device);
        *device = -1;
    }
    return rc;
}

static void log_write(const d2_log_record_t *record)
{
    uint64_t started = hal_time_us();
    w25q32_t flash;
    d2_event_log_t log;
    int device;
    uint32_t slot = 0;
    int read = HAL_ERR_INVALID;
    bool match = false;
    int rc = log_open(&flash, &log, &device);
    if (rc == HAL_OK) {
        slot = (log.next - log.begin) / D2_LOG_SLOT_BYTES;
        rc = d2_log_append(&log, record, LOG_DEADLINE_MS);
        if (rc == HAL_OK) {
            d2_log_record_t back;
            uint8_t a[D2_LOG_WIRE_BYTES], b[D2_LOG_WIRE_BYTES];
            read = d2_log_read(&log, slot, &back);
            match = read == HAL_OK && d2_log_encode(record, a) == HAL_OK &&
                    d2_log_encode(&back, b) == HAL_OK && memcmp(a, b, sizeof a) == 0;
        }
        (void)hal_spi_close(device);
    }
    uint32_t us = (uint32_t)(hal_time_us() - started);
    if (us > s_max_us) s_max_us = us;
    if (rc == HAL_OK && match) {
        count(&s_written);
        printf("[LOG] seq=%u event=%u result=%u reason=%s slot=%u readback=verified write_us=%u\n",
               (unsigned)record->log_seq, (unsigned)record->event_id, (unsigned)record->result_code,
               event_log_reason_name(record->reason_code), (unsigned)slot, (unsigned)us);
        return;
    }
    count(rc == D2_LOG_ERR_FULL ? &s_full : &s_failed);
    s_last_error = rc != HAL_OK ? rc : read != HAL_OK ? read : HAL_ERR_VERIFY;
    printf("[LOG] seq=%u event=%u not stored: %s error=%d read=%d match=%u write_us=%u; occupancy and sampling unaffected\n",
           (unsigned)record->log_seq, (unsigned)record->event_id,
           rc == D2_LOG_ERR_FULL ? "log full" : "flash", rc, read, match ? 1u : 0u, (unsigned)us);
}

void event_log_flush(void)
{
    d2_log_record_t record;
    while (tk_rcv_mbf(log_mbf, &record, TMO_POL) == (INT)sizeof record)
        log_write(&record);
}

void event_log_read_slot(uint32_t slot)
{
    w25q32_t flash;
    d2_event_log_t log;
    int device;
    d2_log_record_t r;
    int rc = log_open(&flash, &log, &device);
    if (rc == HAL_OK) {
        rc = d2_log_read(&log, slot, &r);
        (void)hal_spi_close(device);
    }
    if (rc != HAL_OK) {
        printf("[LOG] slot=%u read error=%d\n", (unsigned)slot, rc);
        return;
    }
    printf("[LOG] slot=%u seq=%u node=%016llx boot=%u event=%u source=%u source_seq=%u "
           "window_us=%llu..%llu quality=%u model=%u occupancy=%ld->%ld confidence_bp=%u "
           "result=%u via=%u class=%u reason=%s valid=%u degraded=%u publish=%u used=%u/%u\n",
           (unsigned)slot, (unsigned)r.log_seq, (unsigned long long)r.node_id, (unsigned)r.boot_id,
           (unsigned)r.event_id, (unsigned)r.source_id, (unsigned)r.source_seq,
           (unsigned long long)r.window_start_us, (unsigned long long)r.window_end_us,
           (unsigned)r.quality_flags, (unsigned)r.model_version,
           (long)r.occupancy_before, (long)r.occupancy_after, (unsigned)r.confidence_bp,
           (unsigned)r.result_code, (unsigned)r.decision_source, (unsigned)r.decided_class,
           event_log_reason_name(r.reason_code), (unsigned)r.event_valid, (unsigned)r.degraded,
           (unsigned)r.publish_status, (unsigned)((log.next - log.begin) / D2_LOG_SLOT_BYTES),
           (unsigned)log.slots);
}

bool event_log_command(const char *line)
{
    if (strncasecmp(line, "LOG", 3) != 0 || (line[3] && line[3] != ' ')) return false;
    const char *arg = line + 3;
    while (*arg == ' ') arg++;
    unsigned long slot;
    char extra;
    if (!*arg || !strcasecmp(arg, "STATUS")) {
        printf("[LOG] enabled=%u offered=%u dropped=%u written=%u failed=%u full=%u last_error=%d "
               "max_write_us=%u area=3FF100-3FFFFF slots=%u\n",
               s_enabled ? 1u : 0u, (unsigned)s_offered, (unsigned)s_dropped, (unsigned)s_written,
               (unsigned)s_failed, (unsigned)s_full, s_last_error, (unsigned)s_max_us,
               (unsigned)LOG_SLOTS);
    } else if (!strcasecmp(arg, "ON")) {
        s_enabled = true;
        puts("[LOG] on: every processed event is written to the W25Q32 at 3FF100-3FFFFF");
    } else if (!strcasecmp(arg, "OFF")) {
        s_enabled = false;
        puts("[LOG] off");
    } else if (!strcasecmp(arg, "FLUSH")) {
        bench_msg_t b = { .name = "LOGFLUSH" };
        if (!runtime_send(bench_mbf, &b, sizeof b)) puts("[LOG] ERROR benchmark queue full");
    } else if (!strncasecmp(arg, "READ ", 5) && sscanf(arg + 5, "%lu %c", &slot, &extra) == 1 &&
               slot < LOG_SLOTS) {
        bench_msg_t b = { .name = "LOGREAD", .value_us = (uint32_t)slot };
        if (!runtime_send(bench_mbf, &b, sizeof b)) puts("[LOG] ERROR benchmark queue full");
    } else {
        printf("[LOG] usage: LOG [STATUS] | LOG ON | LOG OFF | LOG FLUSH | LOG READ <0..%u>\n",
               (unsigned)(LOG_SLOTS - 1));
    }
    return true;
}
