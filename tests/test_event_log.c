/* D2-8 laptop test: events processed by the real ApplicationTask logic are
 * mapped to D2 records, written to a modelled W25Q32 in the board's log area,
 * found again after a modelled reboot and read back field for field. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "application/application_state.h"
#include "application/event_log_map.h"
#include "flash/d2_event_log.h"
#include "flash/w25q32.h"
#include "hal/hal.h"

#define BASE 0x3ff000u          /* the approved scratch sector on the board */
#define BEGIN 0x3ff100u         /* event log area used by event_log_mtk.c */
#define END 0x400000u
#define ID 0xef4016u
#define NODE UINT64_C(0x0000d83bda4a1234)
#define BOOT 12u

static uint8_t memory[4096];
static uint64_t now_us;
static bool wel, absent;
static int busy_polls;

uint64_t hal_time_us(void) { now_us += 100; return now_us; }
void hal_delay_ms(uint32_t ms) { now_us += (uint64_t)ms * 1000; }

int hal_spi_transfer(int device, const uint8_t *tx, uint8_t *rx, size_t len, uint32_t timeout_ms)
{
    assert(device == 3 && tx && len && timeout_ms);
    if (rx) memset(rx, 0, len);
    uint32_t address = len >= 4 ? ((uint32_t)tx[1] << 16) | ((uint32_t)tx[2] << 8) | tx[3] : 0;
    switch (tx[0]) {
    case 0x9f: {
        uint32_t id = absent ? 0xffffffu : ID;
        rx[1] = (uint8_t)(id >> 16); rx[2] = (uint8_t)(id >> 8); rx[3] = (uint8_t)id;
        break;
    }
    case 0x05:
        rx[1] = (uint8_t)((wel ? 2 : 0) | (busy_polls > 0 ? 1 : 0));
        if (busy_polls > 0) --busy_polls;
        break;
    case 0x06:
        wel = true;
        break;
    case 0x03:
        assert(address >= BASE && address + len - 4 <= BASE + sizeof memory);
        memcpy(rx + 4, memory + address - BASE, len - 4);
        break;
    case 0x02:
        assert(wel && address >= BEGIN && address + len - 4 <= END);
        for (size_t i = 4; i < len; ++i) memory[address - BASE + i - 4] &= tx[i];
        wel = false;
        busy_polls = 2;
        break;
    default:
        assert(!"unexpected flash command");
    }
    return (int)len;
}

static event_record_t make_event(uint32_t id, uint32_t seq, event_class_t rule)
{
    event_record_t e = { 0 };
    e.schema_version = EVENT_RECORD_SCHEMA_VERSION;
    e.event_id = id;
    e.source_id = 1;
    e.source_seq = seq;
    e.window_start_us = now_us > 1000 ? now_us - 1000 : 1;
    e.window_end_us = now_us;
    e.quality_flags = EVENT_QUALITY_SEQUENCE_COMPLETE | EVENT_QUALITY_TIMING_VALID |
                      EVENT_QUALITY_RULE_VALID;
    e.valid = 1;
    e.rule_class = (uint8_t)rule;
    return e;
}

static void same(const d2_log_record_t *a, const d2_log_record_t *b)
{
    uint8_t wa[D2_LOG_WIRE_BYTES], wb[D2_LOG_WIRE_BYTES];
    assert(d2_log_encode(a, wa) == HAL_OK && d2_log_encode(b, wb) == HAL_OK);
    assert(memcmp(wa, wb, sizeof wa) == 0);
}

int main(void)
{
    memset(memory, 0xff, sizeof memory);
    now_us = 10000000;

    /* Reason codes are fixed and round-trip; validation reasons share one code. */
    for (uint8_t c = EVENT_LOG_REASON_ACCEPTED; c <= EVENT_LOG_REASON_INVALID_EVENT; ++c)
        assert(c == EVENT_LOG_REASON_INVALID_EVENT ||
               event_log_reason_code(event_log_reason_name(c)) == c);
    assert(event_log_reason_code(NULL) == EVENT_LOG_REASON_NONE);
    assert(event_log_reason_code("stale") == EVENT_LOG_REASON_INVALID_EVENT);

    /* Process events with the real application logic, as ApplicationTask does. */
    application_state_t app;
    application_state_init(&app, NULL);
    event_record_t events[5] = {
        make_event(91, 1, EVENT_CLASS_ENTRY),
        make_event(91, 1, EVENT_CLASS_ENTRY),      /* the same event again */
        make_event(92, 2, EVENT_CLASS_EXIT),
        make_event(93, 3, EVENT_CLASS_ENTRY),
        make_event(94, 4, EVENT_CLASS_ENTRY),
    };
    events[3].valid = 0;                            /* invalid: rejected */
    events[4].quality_flags |= EVENT_QUALITY_ML_VALID;
    events[4].ml_class = EVENT_CLASS_ENTRY;
    events[4].ml_confidence = 0.87f;               /* decided by ML */
    d2_log_record_t records[5];
    const uint8_t publish[5] = { D2_LOG_PUBLISH_ENQUEUED, D2_LOG_PUBLISH_ENQUEUED,
                                 D2_LOG_PUBLISH_FAILED, D2_LOG_PUBLISH_ENQUEUED,
                                 D2_LOG_PUBLISH_NOT_ATTEMPTED };
    for (unsigned i = 0; i < 5; ++i) {
        application_result_t r = application_process_event(&app, &events[i], now_us);
        event_log_map(&events[i], &r, publish[i], NODE, BOOT, i + 1, &records[i]);
    }
    assert(records[0].result_code == D2_LOG_APPLIED && records[0].occupancy_before == 0 &&
           records[0].occupancy_after == 1 && records[0].decision_source == 1 &&
           records[0].decided_class == EVENT_CLASS_ENTRY &&
           records[0].reason_code == EVENT_LOG_REASON_ACCEPTED &&
           records[0].confidence_bp == UINT16_MAX && records[0].event_valid == 1);
    assert(records[1].result_code == D2_LOG_DUPLICATE &&
           records[1].reason_code == EVENT_LOG_REASON_DUPLICATE && records[1].occupancy_after == 1);
    assert(records[2].result_code == D2_LOG_APPLIED && records[2].occupancy_after == 0 &&
           records[2].publish_status == D2_LOG_PUBLISH_FAILED);
    assert(records[3].result_code == D2_LOG_REJECTED && records[3].event_valid == 0 &&
           records[3].reason_code == EVENT_LOG_REASON_INVALID_EVENT && records[3].degraded == 1);
    assert(records[4].decision_source == 2 && records[4].confidence_bp == 8700 &&
           records[4].occupancy_after == 1);

    /* Without the node identity a record is refused, never written half-made. */
    d2_log_record_t anonymous;
    application_state_t other;
    application_state_init(&other, NULL);
    application_result_t r0 = application_process_event(&other, &events[0], now_us);
    event_log_map(&events[0], &r0, D2_LOG_PUBLISH_ENQUEUED, 0, 0, 1, &anonymous);
    uint8_t wire[D2_LOG_WIRE_BYTES];
    assert(d2_log_encode(&anonymous, wire) == HAL_ERR_INVALID);

    /* Missing module: the open fails with an error and nothing is written. */
    w25q32_t flash;
    absent = true;
    assert(w25q32_probe(&flash, 3, ID) == HAL_ERR_ABSENT);
    absent = false;

    /* SPILOGWRITE's fixture may already hold slot 0; the log continues after it. */
    const d2_log_record_t fixture = { .node_id = UINT64_C(0xd200000000000001), .boot_id = 0xd2000001u,
                                      .log_seq = 1, .confidence_bp = UINT16_MAX,
                                      .result_code = D2_LOG_REJECTED, .reason_code = 0xfeu };
    assert(w25q32_probe(&flash, 3, ID) == HAL_OK);
    d2_event_log_t log;
    assert(d2_log_open(&log, &flash, BEGIN, END) == HAL_OK && log.slots == 30);
    assert(d2_log_append(&log, &fixture, 200) == HAL_OK);

    /* Write each record as BenchmarkTask does: open, append, read back, compare. */
    for (unsigned i = 0; i < 5; ++i) {
        assert(d2_log_open(&log, &flash, BEGIN, END) == HAL_OK);
        uint32_t slot = (log.next - log.begin) / D2_LOG_SLOT_BYTES;
        assert(slot == i + 1);
        assert(d2_log_append(&log, &records[i], 200) == HAL_OK);
        d2_log_record_t back;
        assert(d2_log_read(&log, slot, &back) == HAL_OK);
        same(&records[i], &back);
    }

    /* After a reboot every record is found and read back unchanged. */
    assert(d2_log_open(&log, &flash, BEGIN, END) == HAL_OK);
    assert(log.valid == 6 && log.invalid == 0 && log.next == BEGIN + 6 * D2_LOG_SLOT_BYTES);
    for (unsigned i = 0; i < 5; ++i) {
        d2_log_record_t back;
        assert(d2_log_read(&log, i + 1, &back) == HAL_OK);
        same(&records[i], &back);
        assert(back.node_id == NODE && back.boot_id == BOOT && back.log_seq == i + 1 &&
               back.event_id == events[i].event_id && back.source_seq == events[i].source_seq);
    }

    /* The area holds 30 records; the 31st is refused as full, not wrapped. */
    d2_log_record_t more = records[1];
    while (log.next < log.end) {
        ++more.log_seq;
        assert(d2_log_append(&log, &more, 200) == HAL_OK);
    }
    ++more.log_seq;
    assert(d2_log_append(&log, &more, 200) == D2_LOG_ERR_FULL);

    puts("event log: PASS (ApplicationTask results mapped, stored, recovered after reboot and read back; "
         "missing module and full area reported)");
    return 0;
}
