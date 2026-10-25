#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "flash/w25q32.h"
#include "hal/hal.h"

#define SCRATCH 0x1000u
#define ID 0xef4016u /* fixture ID for the mock, not an approved real part */

static uint8_t memory[256];
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

    puts("w25q32 mock: PASS (probe, program/readback, bounds, absent, wrong ID, timeout, busy, partial program, corruption)");
    return 0;
}
