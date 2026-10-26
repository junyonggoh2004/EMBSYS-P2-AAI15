// D2 transport-only benchmark on the µT-Kernel firmware. It deliberately
// uses the same HAL as bus_spi and never treats a loopback as flash proof.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <tk/tkernel.h>
#include "esp_heap_caps.h"
#include "flash/w25q32.h"
#include "flash/d2_event_log.h"
#include "hal/hal.h"
#include "runtime_mtk.h"

#define ATTEMPTS 1000u
#define WARMUPS 10u
#define MAX_BYTES 1024u
#define D2_SCRATCH_BEGIN 0x3ff000u
#define D2_SCRATCH_END 0x400000u
#define D2_PART_ID 0xef4016u
#define D2_LOG_TEST_BEGIN 0x3ff100u

static const size_t s_sizes[] = { 8, 16, 32, 64, 128, 256, 512, 1024 };
static uint8_t s_tx[MAX_BYTES], s_rx[MAX_BYTES];
static uint32_t s_latencies[ATTEMPTS];
static const uint8_t s_flash_test_pattern[16] = {
    'D', '2', '-', 'S', 'P', 'I', '-', 'W', '2', '5', 'Q', '3', '2', '-', '0', '1'
};

/* A rejected, invalid synthetic fixture: never report this as a live event. */
static const d2_log_record_t s_log_test_record = {
    .node_id = UINT64_C(0xd200000000000001),
    .boot_id = 0xd2000001u,
    .log_seq = 1,
    .confidence_bp = UINT16_MAX,
    .result_code = D2_LOG_REJECTED,
    .reason_code = 0xfeu, /* reserved test-only marker */
    .publish_status = D2_LOG_PUBLISH_NOT_ATTEMPTED,
};

static int log_test_open(w25q32_t *flash, d2_event_log_t *log)
{
    int device = hal_spi_open("fspi", 11, 12, 13, 10, 1000000, 0);
    if (device < 0) return device;
    int rc = w25q32_probe(flash, device, D2_PART_ID);
    if (rc == HAL_OK)
        rc = d2_log_open(log, flash, D2_LOG_TEST_BEGIN, D2_SCRATCH_END);
    if (rc != HAL_OK) {
        (void)hal_spi_close(device);
        return rc;
    }
    return device;
}

static void log_test_print_bytes(const uint8_t wire[D2_LOG_WIRE_BYTES])
{
    static const char hex[] = "0123456789ABCDEF";
    char out[D2_LOG_WIRE_BYTES * 2 + 1];
    for (size_t i = 0; i < D2_LOG_WIRE_BYTES; ++i) {
        out[i * 2] = hex[wire[i] >> 4];
        out[i * 2 + 1] = hex[wire[i] & 15u];
    }
    out[sizeof out - 1] = 0;
    printf("[SPILOG] fixture address=3FF100 bytes=80 hex=%s\n", out);
}

void mtk_spi_flash_log_plan(void)
{
    uint8_t wire[D2_LOG_WIRE_BYTES];
    int encoded = d2_log_encode(&s_log_test_record, wire);
    if (encoded != HAL_OK) {
        printf("[SPILOGPLAN] ERROR encode=%d; no write\n", encoded);
        return;
    }
    w25q32_t flash;
    d2_event_log_t log;
    int device = log_test_open(&flash, &log);
    if (device < 0) {
        printf("[SPILOGPLAN] ERROR open=%d; no write\n", device);
        return;
    }
    bool blank = log.valid == 0 && log.invalid == 0 &&
                 log.next == D2_LOG_TEST_BEGIN;
    int closed = hal_spi_close(device);
    log_test_print_bytes(wire);
    printf("[SPILOGPLAN] %s slots=%lu valid=%lu invalid=%lu close=%d; no write\n",
           blank && closed == HAL_OK ? "BLANK" : "NOT_BLANK",
           (unsigned long)log.slots, (unsigned long)log.valid,
           (unsigned long)log.invalid, closed);
}

void mtk_spi_flash_log_write(void)
{
    w25q32_t flash;
    d2_event_log_t log;
    int device = log_test_open(&flash, &log);
    if (device < 0) {
        printf("[SPILOGWRITE] ERROR open=%d; no write\n", device);
        return;
    }
    if (log.valid || log.invalid || log.next != D2_LOG_TEST_BEGIN) {
        printf("[SPILOGWRITE] ERROR area not blank valid=%lu invalid=%lu; no write\n",
               (unsigned long)log.valid, (unsigned long)log.invalid);
        (void)hal_spi_close(device);
        return;
    }
    uint64_t started = hal_time_us();
    int written = d2_log_append(&log, &s_log_test_record, 200);
    uint32_t elapsed = (uint32_t)(hal_time_us() - started);
    d2_log_record_t readback;
    int read = written == HAL_OK ? d2_log_read(&log, 0, &readback) : written;
    uint8_t expected[D2_LOG_WIRE_BYTES], actual[D2_LOG_WIRE_BYTES];
    int match = read == HAL_OK &&
                d2_log_encode(&s_log_test_record, expected) == HAL_OK &&
                d2_log_encode(&readback, actual) == HAL_OK &&
                memcmp(expected, actual, sizeof expected) == 0;
    int closed = hal_spi_close(device);
    if (written == HAL_OK && read == HAL_OK && match && closed == HAL_OK)
        printf("[SPILOGWRITE] PASS address=3FF100 bytes=80 readback=verified elapsed_us=%lu erase=none\n",
               (unsigned long)elapsed);
    else
        printf("[SPILOGWRITE] ERROR write=%d read=%d match=%d close=%d elapsed_us=%lu\n",
               written, read, match, closed, (unsigned long)elapsed);
}

void mtk_spi_flash_log_verify(void)
{
    w25q32_t flash;
    d2_event_log_t log;
    int device = log_test_open(&flash, &log);
    if (device < 0) {
        printf("[SPILOGVERIFY] ERROR open=%d; no write\n", device);
        return;
    }
    d2_log_record_t recovered;
    int read = d2_log_read(&log, 0, &recovered);
    uint8_t expected[D2_LOG_WIRE_BYTES], actual[D2_LOG_WIRE_BYTES];
    int match = read == HAL_OK &&
                d2_log_encode(&s_log_test_record, expected) == HAL_OK &&
                d2_log_encode(&recovered, actual) == HAL_OK &&
                memcmp(expected, actual, sizeof expected) == 0;
    int closed = hal_spi_close(device);
    if (log.valid == 1 && log.invalid == 0 && read == HAL_OK &&
        match && closed == HAL_OK)
        puts("[SPILOGVERIFY] PASS address=3FF100 bytes=80 crc=valid synthetic=1; no write");
    else
        printf("[SPILOGVERIFY] ERROR valid=%lu invalid=%lu read=%d match=%d close=%d; no write\n",
               (unsigned long)log.valid, (unsigned long)log.invalid,
               read, match, closed);
}

static void pattern(size_t n, unsigned trial)
{
    for (size_t i = 0; i < n; ++i) {
        s_tx[i] = (uint8_t)((i * 73u + trial * 29u + (i >> 3)) ^ 0xa5u);
        s_rx[i] = 0;
    }
}

static unsigned mismatch(size_t n)
{
    unsigned bad = 0;
    for (size_t i = 0; i < n; ++i) if (s_rx[i] != s_tx[i]) ++bad;
    return bad;
}

static void print_samples(size_t n)
{
    static char line[700];
    for (unsigned start = 0; start < ATTEMPTS; start += 50) {
        int used = snprintf(line, sizeof line,
                            "[SPIBENCH] SAMPLES bytes=%u start=%u values=",
                            (unsigned)n, start);
        for (unsigned i = start; i < start + 50 && used > 0 && (size_t)used < sizeof line; ++i)
            used += snprintf(line + used, sizeof line - (size_t)used,
                             "%s%lu", i == start ? "" : ",",
                             (unsigned long)s_latencies[i]);
        if (used <= 0 || (size_t)used >= sizeof line - 2) {
            puts("[SPIBENCH] ERROR sample line overflow");
            return;
        }
        line[used++] = '\n';
        line[used] = 0;
        printf("%s", line);
        runtime_alive(RT_BENCHMARK);
        tk_dly_tsk(1);
    }
}

// Read-only discovery before choosing an expected part ID or scratch sector.
// 0x9F is a JEDEC-ID read; this path never issues write-enable, program or erase.
void mtk_spi_flash_id(void)
{
    int device = hal_spi_open("fspi", 11, 12, 13, 10, 1000000, 0);
    if (device < 0) {
        printf("[SPIID] ERROR open=%d\n", device);
        return;
    }
    const uint8_t tx[4] = { 0x9f, 0, 0, 0 };
    uint8_t rx[4] = { 0 };
    int result = hal_spi_transfer(device, tx, rx, sizeof tx, 100);
    int closed = hal_spi_close(device);
    if (result != sizeof tx || closed != HAL_OK) {
        printf("[SPIID] ERROR transfer=%d close=%d\n", result, closed);
        return;
    }
    if ((rx[1] == 0 && rx[2] == 0 && rx[3] == 0) ||
        (rx[1] == 0xff && rx[2] == 0xff && rx[3] == 0xff)) {
        printf("[SPIID] INVALID raw=%02X%02X%02X; check module and wiring\n",
               rx[1], rx[2], rx[3]);
        return;
    }
    printf("[SPIID] raw=%02X%02X%02X; verify against chip marking before writes\n",
           rx[1], rx[2], rx[3]);
}

// Confirms the device layer can identify and repeatedly read the same bytes.
// This deliberately makes no write or erase request to an unallocated sector.
void mtk_spi_flash_check(uint32_t expected_id)
{
    int device = hal_spi_open("fspi", 11, 12, 13, 10, 1000000, 0);
    if (device < 0) {
        printf("[SPICHECK] ERROR open=%d\n", device);
        return;
    }
    w25q32_t flash;
    uint8_t first[16], second[16];
    int probe = w25q32_probe(&flash, device, expected_id);
    int read1 = probe == HAL_OK ? w25q32_read(&flash, 0, first, sizeof first) : probe;
    int read2 = read1 == HAL_OK ? w25q32_read(&flash, 0, second, sizeof second) : read1;
    int closed = hal_spi_close(device);
    if (probe != HAL_OK || read1 != HAL_OK || read2 != HAL_OK ||
        closed != HAL_OK || memcmp(first, second, sizeof first) != 0) {
        printf("[SPICHECK] ERROR probe=%d read1=%d read2=%d close=%d\n",
               probe, read1, read2, closed);
        return;
    }
    static const char digits[] = "0123456789ABCDEF";
    char hex[sizeof first * 2 + 1];
    for (size_t i = 0; i < sizeof first; ++i) {
        hex[i * 2] = digits[first[i] >> 4];
        hex[i * 2 + 1] = digits[first[i] & 15];
    }
    hex[sizeof first * 2] = 0;
    printf("[SPICHECK] PASS read-only id=%06lX address=000000 bytes=16 data=%s\n",
           (unsigned long)expected_id, hex);
}

// Read-only audit of the proposed final 4 KiB sector. A BLANK result is
// necessary before a later program test, but does not itself approve writes.
void mtk_spi_flash_scan(uint32_t expected_id)
{
    enum { SCRATCH_BYTES = D2_SCRATCH_END - D2_SCRATCH_BEGIN };
    int device = hal_spi_open("fspi", 11, 12, 13, 10, 1000000, 0);
    if (device < 0) {
        printf("[SPISCAN] ERROR open=%d\n", device);
        return;
    }
    w25q32_t flash;
    int rc = w25q32_probe(&flash, device, expected_id);
    unsigned nonblank = 0;
    uint32_t first_nonblank = 0;
    uint8_t data[256];
    for (uint32_t offset = 0; rc == HAL_OK && offset < SCRATCH_BYTES; offset += sizeof data) {
        rc = w25q32_read(&flash, D2_SCRATCH_BEGIN + offset, data, sizeof data);
        if (rc != HAL_OK) break;
        for (size_t i = 0; i < sizeof data; ++i) {
            if (data[i] == 0xff) continue;
            if (!nonblank) first_nonblank = D2_SCRATCH_BEGIN + offset + i;
            ++nonblank;
        }
        runtime_alive(RT_BENCHMARK);
    }
    int closed = hal_spi_close(device);
    if (rc != HAL_OK || closed != HAL_OK) {
        printf("[SPISCAN] ERROR read=%d close=%d\n", rc, closed);
    } else if (nonblank) {
        printf("[SPISCAN] NONBLANK sector=3FF000 bytes=4096 non_ff=%u first=%06lX; no writes\n",
               nonblank, (unsigned long)first_nonblank);
    } else {
        puts("[SPISCAN] BLANK sector=3FF000 bytes=4096; no writes made");
    }
}

// One-time hardware acceptance at the user-approved address. The device layer
// rejects non-FF content and verifies status, complete program, readback and ID.
void mtk_spi_flash_program(uint32_t expected_id)
{
    if (expected_id != D2_PART_ID) {
        puts("[SPIPROGRAM] ERROR unexpected ID; no write");
        return;
    }
    int device = hal_spi_open("fspi", 11, 12, 13, 10, 1000000, 0);
    if (device < 0) {
        printf("[SPIPROGRAM] ERROR open=%d; no write\n", device);
        return;
    }
    w25q32_t flash;
    int probe = w25q32_probe(&flash, device, expected_id);
    int program = probe == HAL_OK ?
        w25q32_program_verified(&flash, D2_SCRATCH_BEGIN, s_flash_test_pattern,
                                sizeof s_flash_test_pattern, D2_SCRATCH_BEGIN,
                                D2_SCRATCH_END, 100) : probe;
    int closed = hal_spi_close(device);
    if (probe == HAL_OK && program == HAL_OK && closed == HAL_OK)
        puts("[SPIPROGRAM] PASS address=3FF000 bytes=16 readback=verified erase=none");
    else
        printf("[SPIPROGRAM] ERROR probe=%d program=%d close=%d\n",
               probe, program, closed);
}

// May be run after a reboot: the expected pattern is compiled into firmware,
// while the read comes afresh from the external module.
void mtk_spi_flash_verify(uint32_t expected_id)
{
    if (expected_id != D2_PART_ID) {
        puts("[SPIVERIFY] ERROR unexpected ID");
        return;
    }
    int device = hal_spi_open("fspi", 11, 12, 13, 10, 1000000, 0);
    if (device < 0) {
        printf("[SPIVERIFY] ERROR open=%d\n", device);
        return;
    }
    w25q32_t flash;
    uint8_t data[sizeof s_flash_test_pattern];
    int probe = w25q32_probe(&flash, device, expected_id);
    int read = probe == HAL_OK ? w25q32_read(&flash, D2_SCRATCH_BEGIN,
                                            data, sizeof data) : probe;
    int closed = hal_spi_close(device);
    if (probe == HAL_OK && read == HAL_OK && closed == HAL_OK &&
        memcmp(data, s_flash_test_pattern, sizeof data) == 0)
        puts("[SPIVERIFY] PASS address=3FF000 bytes=16 pattern=retained");
    else
        printf("[SPIVERIFY] ERROR probe=%d read=%d close=%d match=%d\n",
               probe, read, closed,
               read == HAL_OK ? memcmp(data, s_flash_test_pattern, sizeof data) == 0 : 0);
}

void mtk_spi_benchmark(unsigned mode, uint32_t hz)
{
    if (mode > 3 || hz < 1000000 || hz > 20000000) {
        puts("[SPIBENCH] ERROR invalid mode or clock");
        return;
    }
    int device = hal_spi_open("fspi", 11, 12, 13, 10, hz, (uint8_t)mode);
    if (device < 0) {
        printf("[SPIBENCH] ERROR open=%d\n", device);
        return;
    }
    unsigned failed_sizes = 0;
    printf("[SPIBENCH] BEGIN mode=%u hz=%lu sclk=11 mosi=12 miso=13 cs=10 warmups=%u attempts=%u heap_before=%lu\n",
           mode, (unsigned long)hz, WARMUPS, ATTEMPTS,
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    for (size_t si = 0; si < sizeof s_sizes / sizeof s_sizes[0]; ++si) {
        size_t n = s_sizes[si];
        unsigned warmup_errors = 0;
        for (unsigned i = 0; i < WARMUPS; ++i) {
            pattern(n, i);
            int rc = hal_spi_transfer(device, s_tx, s_rx, n, 1000);
            if (rc != (int)n || mismatch(n)) ++warmup_errors;
        }
        unsigned platform_errors = 0, timeouts = 0, failed = 0;
        unsigned mismatch_transactions = 0, mismatch_bytes = 0;
        uint64_t total_us = 0;
        uint32_t max_us = 0;
        for (unsigned i = 0; i < ATTEMPTS; ++i) {
            pattern(n, i + WARMUPS);
            uint64_t start = hal_time_us();
            int rc = hal_spi_transfer(device, s_tx, s_rx, n, 1000);
            uint64_t elapsed = hal_time_us() - start;
            s_latencies[i] = (uint32_t)elapsed;
            total_us += elapsed;
            if (elapsed > max_us) max_us = (uint32_t)elapsed;
            if (rc != (int)n) {
                ++platform_errors;
                if (rc == HAL_ERR_TIMEOUT) ++timeouts;
            }
            unsigned bad = mismatch(n);
            if (rc != (int)n || bad) ++failed;
            if (bad) ++mismatch_transactions;
            mismatch_bytes += bad;
            if ((i & 31u) == 31u) {
                runtime_alive(RT_BENCHMARK);
                tk_dly_tsk(1);
            }
        }
        if (warmup_errors || failed) ++failed_sizes;
        printf("[SPIBENCH] SIZE bytes=%u warmup_errors=%u attempts=%u failed_transactions=%u platform_errors=%u timeouts=%u mismatch_transactions=%u mismatch_bytes=%u total_us=%llu max_us=%lu heap_after=%lu\n",
               (unsigned)n, warmup_errors, ATTEMPTS, failed, platform_errors,
               timeouts, mismatch_transactions, mismatch_bytes,
               (unsigned long long)total_us, (unsigned long)max_us,
               (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        print_samples(n);
    }
    int closed = hal_spi_close(device);
    printf("[SPIBENCH] END failed_sizes=%u close=%d heap_after=%lu\n",
           failed_sizes, closed,
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
