#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "flash/w25q32.h"
#include "flash/d2_event_log.h"
#include "hal/hal.h"

#define SCRATCH 0x1000u
#define ID 0xef4016u /* fixture ID for the mock, not an approved real part */

static uint8_t memory[4096];
static uint64_t now_us;
static bool wel, program_started, stuck_busy, corrupt_read, partial_program;
static bool absent, wrong_id, transport_timeout;
static int busy_polls;

static void reset_mock(void)
{
    memset(memory, 0xff, sizeof memory);
    now_us = 0;
    wel = program_started = stuck_busy = corrupt_read = partial_program = false;
    absent = wrong_id = transport_timeout = false;
    busy_polls = 0;
}

uint64_t hal_time_us(void) { now_us += 100; return now_us; }
void hal_delay_ms(uint32_t ms) { now_us += (uint64_t)ms * 1000; }

int hal_spi_transfer(int device, const uint8_t *tx, uint8_t *rx, size_t len,
                     uint32_t timeout_ms)
{
    assert(device == 2 && tx && len && timeout_ms);
    if (transport_timeout) return HAL_ERR_TIMEOUT;
    if (rx) memset(rx, 0, len);
    switch (tx[0]) {
    case 0x9f: {
        assert(len == 4 && rx);
        uint32_t id = absent ? 0xffffffu : (wrong_id ? 0xc84016u : ID);
        rx[1] = (uint8_t)(id >> 16);
        rx[2] = (uint8_t)(id >> 8);
        rx[3] = (uint8_t)id;
        break;
    }
    case 0x05:
        assert(len == 2 && rx);
        rx[1] = (uint8_t)((wel ? 2 : 0) | (((stuck_busy && program_started) || busy_polls > 0) ? 1 : 0));
        if (busy_polls > 0) --busy_polls;
        break;
    case 0x06:
        assert(len == 1);
        wel = true;
        break;
    case 0x03: {
        assert(len >= 5 && rx);
        uint32_t address = ((uint32_t)tx[1] << 16) | ((uint32_t)tx[2] << 8) | tx[3];
        assert(address >= SCRATCH && address + len - 4 <= SCRATCH + sizeof memory);
        memcpy(rx + 4, memory + address - SCRATCH, len - 4);
        if (corrupt_read && memory[address - SCRATCH] != 0xff) rx[4] ^= 1;
        break;
    }
    case 0x02: {
        assert(len >= 5 && wel);
        uint32_t address = ((uint32_t)tx[1] << 16) | ((uint32_t)tx[2] << 8) | tx[3];
        assert(address >= SCRATCH && address + len - 4 <= SCRATCH + sizeof memory);
        size_t last = partial_program ? 4 + (len - 4) / 2 : len;
        for (size_t i = 4; i < last; ++i) memory[address - SCRATCH + i - 4] &= tx[i];
        wel = false;
        program_started = true;
        busy_polls = 2;
        break;
    }
    default:
        assert(!"unexpected flash command");
    }
    return (int)len;
}

int main(void)
{
    w25q32_t flash;
    const uint8_t payload[] = { 0x12, 0x34, 0x56, 0x78 };
    uint8_t out[sizeof payload];

    reset_mock();
    assert(w25q32_probe(&flash, 2, ID) == HAL_OK && flash.jedec_id == ID);
    assert(w25q32_program_verified(&flash, SCRATCH, payload, sizeof payload,
                                   SCRATCH, SCRATCH + 256, 20) == HAL_OK);
    assert(w25q32_read(&flash, SCRATCH, out, sizeof out) == HAL_OK);
    assert(memcmp(out, payload, sizeof out) == 0);
    assert(w25q32_program_verified(&flash, SCRATCH, payload, sizeof payload,
                                   SCRATCH, SCRATCH + 256, 20) == HAL_ERR_BUSY);
    assert(w25q32_program_verified(&flash, SCRATCH + 255, payload, sizeof payload,
                                   SCRATCH, SCRATCH + 256, 20) == HAL_ERR_INVALID);

    reset_mock(); absent = true;
    assert(w25q32_probe(&flash, 2, ID) == HAL_ERR_ABSENT);
    assert(flash.jedec_id == 0);
    reset_mock(); wrong_id = true;
    assert(w25q32_probe(&flash, 2, ID) == HAL_ERR_VERIFY);
    reset_mock(); transport_timeout = true;
    assert(w25q32_probe(&flash, 2, ID) == HAL_ERR_TIMEOUT);

    reset_mock();
    assert(w25q32_probe(&flash, 2, ID) == HAL_OK);
    stuck_busy = true;
    assert(w25q32_program_verified(&flash, SCRATCH, payload, sizeof payload,
                                   SCRATCH, SCRATCH + 256, 3) == HAL_ERR_TIMEOUT);

    reset_mock();
    assert(w25q32_probe(&flash, 2, ID) == HAL_OK);
    corrupt_read = true;
    assert(w25q32_program_verified(&flash, SCRATCH, payload, sizeof payload,
                                   SCRATCH, SCRATCH + 256, 20) == HAL_ERR_VERIFY);

    reset_mock();
    assert(w25q32_probe(&flash, 2, ID) == HAL_OK);
    partial_program = true;
    assert(w25q32_program_verified(&flash, SCRATCH, payload, sizeof payload,
                                   SCRATCH, SCRATCH + 256, 20) == HAL_ERR_VERIFY);

    reset_mock();
    assert(w25q32_probe(&flash, 2, ID) == HAL_OK);
    absent = true;
    assert(w25q32_program_verified(&flash, SCRATCH, payload, sizeof payload,
                                   SCRATCH, SCRATCH + 256, 20) == HAL_ERR_ABSENT);

    /* Explicit wire bytes and CRC, including a signed occupancy outcome. */
    d2_log_record_t event = {
        .node_id = UINT64_C(0x1122334455667788), .boot_id = 7,
        .log_seq = 1, .event_id = 9, .source_id = 4, .source_seq = 3,
        .window_start_us = 1000, .window_end_us = 2000,
        .quality_flags = 3, .model_version = 2,
        .occupancy_before = 4, .occupancy_after = 5,
        .confidence_bp = 8750, .result_code = D2_LOG_APPLIED,
        .decision_source = 1, .decided_class = 1, .event_valid = 1,
        .publish_status = D2_LOG_PUBLISH_ENQUEUED
    };
    uint8_t wire[D2_LOG_WIRE_BYTES];
    d2_log_record_t decoded;
    assert(d2_log_encode(&event, wire) == HAL_OK);
    assert(memcmp(wire, "D2LG", 4) == 0);
    assert(wire[4] == D2_LOG_VERSION && wire[5] == D2_LOG_WIRE_BYTES);
    assert(wire[8] == 0x88 && wire[15] == 0x11);
    /* Independent Python zlib.crc32/struct.pack reference for bytes 0..75. */
    assert(wire[76] == 0x54 && wire[77] == 0xec &&
           wire[78] == 0x60 && wire[79] == 0xf2);
    assert(d2_log_decode(wire, &decoded) == HAL_OK);
    assert(decoded.node_id == event.node_id && decoded.event_id == 9);
    assert(decoded.occupancy_before == 4 && decoded.occupancy_after == 5);
    wire[60] ^= 1;
    assert(d2_log_decode(wire, &decoded) == HAL_ERR_VERIFY);
    assert(d2_log_encode(&event, wire) == HAL_OK);
    wire[4] = 2;
    assert(d2_log_decode(wire, &decoded) == HAL_ERR_VERIFY);
    for (uint8_t result = D2_LOG_NO_CHANGE;
         result <= D2_LOG_REJECTED; ++result) {
        d2_log_record_t processed = event;
        processed.result_code = result;
        processed.occupancy_after = processed.occupancy_before;
        processed.publish_status = D2_LOG_PUBLISH_NOT_ATTEMPTED;
        assert(d2_log_encode(&processed, wire) == HAL_OK);
        assert(d2_log_decode(wire, &decoded) == HAL_OK);
        assert(decoded.result_code == result &&
               decoded.publish_status == D2_LOG_PUBLISH_NOT_ATTEMPTED);
    }
    event.event_valid = 0;
    assert(d2_log_encode(&event, wire) == HAL_ERR_INVALID);
    event.event_valid = 1;

    reset_mock();
    assert(w25q32_probe(&flash, 2, ID) == HAL_OK);
    d2_event_log_t log;
    const uint32_t begin = SCRATCH + 256;
    const uint32_t end = SCRATCH + sizeof memory;
    assert(d2_log_open(&log, &flash, SCRATCH, SCRATCH + 8192) == HAL_ERR_INVALID);
    assert(d2_log_open(&log, &flash, begin + 1, end) == HAL_ERR_INVALID);
    assert(d2_log_open(&log, &flash, begin, end) == HAL_OK);
    assert(log.slots == 30 && log.valid == 0 && log.invalid == 0);
    assert(d2_log_append(&log, &event, 20) == HAL_OK);
    assert(log.valid == 1 && log.next == begin + D2_LOG_SLOT_BYTES);
    assert(d2_log_read(&log, 0, &decoded) == HAL_OK);
    assert(decoded.source_seq == 3 && decoded.publish_status == D2_LOG_PUBLISH_ENQUEUED);
    assert(d2_log_open(&log, &flash, begin, end) == HAL_OK);
    assert(log.valid == 1 && log.next == begin + D2_LOG_SLOT_BYTES);

    /* Reboot scan must skip a torn slot and never reuse its bytes. */
    memory[256 + D2_LOG_SLOT_BYTES] = 0;
    assert(d2_log_open(&log, &flash, begin, end) == HAL_OK);
    assert(log.valid == 1 && log.invalid == 1);
    assert(log.next == begin + 2 * D2_LOG_SLOT_BYTES);
    assert(d2_log_read(&log, 1, &decoded) == HAL_ERR_VERIFY);
    d2_log_record_t rejected = event;
    rejected.log_seq = 2;
    rejected.event_id = rejected.source_id = rejected.source_seq = 0;
    rejected.window_start_us = rejected.window_end_us = 0;
    rejected.result_code = D2_LOG_REJECTED;
    rejected.reason_code = 5;
    rejected.event_valid = 0;
    rejected.degraded = 1;
    rejected.publish_status = D2_LOG_PUBLISH_NOT_ATTEMPTED;
    rejected.occupancy_after = rejected.occupancy_before;
    assert(d2_log_append(&log, &rejected, 20) == HAL_OK);
    assert(d2_log_open(&log, &flash, begin, end) == HAL_OK);
    assert(log.valid == 2 && log.invalid == 1);
    assert(d2_log_read(&log, 2, &decoded) == HAL_OK);
    assert(decoded.result_code == D2_LOG_REJECTED && decoded.event_valid == 0);

    /* A failed page program is never reported as stored, nor retried on the
     * same possibly torn slot. The following good event uses the next slot. */
    partial_program = true;
    rejected.log_seq = 3;
    assert(d2_log_append(&log, &rejected, 20) == HAL_ERR_VERIFY);
    assert(log.write_errors == 1 && log.invalid == 1);
    partial_program = false;
    rejected.log_seq = 4;
    assert(d2_log_append(&log, &rejected, 20) == HAL_OK);
    assert(d2_log_open(&log, &flash, begin, end) == HAL_OK);
    assert(log.valid == 3 && log.invalid == 2);

    /* Exhaust the approved non-erasing area, then refuse another event. */
    for (unsigned i = 0; i < 40 && log.next < end; ++i) {
        ++rejected.log_seq;
        assert(d2_log_append(&log, &rejected, 20) == HAL_OK);
    }
    assert(log.next == end);
    assert(d2_log_append(&log, &rejected, 20) == D2_LOG_ERR_FULL);
    assert(d2_log_open(&log, &flash, begin, end) == HAL_OK);
    assert(log.valid + log.invalid == log.slots);

    puts("w25q32/log mock: PASS (device faults, codec, CRC, recovery, torn slot, full log)");
    return 0;
}
