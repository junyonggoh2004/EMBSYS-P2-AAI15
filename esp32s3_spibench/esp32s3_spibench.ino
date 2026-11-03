// Dedicated D2 loopback benchmark. GPIO12 (MOSI) must be jumpered to
// GPIO13 (MISO); GPIO11 is SCLK and GPIO10 is CS. Keep the TFT disconnected.
// This sketch calls the same SPI HAL used by the production bus driver.
#include <EdgeS3.h>
#include <stdio.h>
#include <string.h>

static constexpr int kSclk = 11;
static constexpr int kMosi = 12;
static constexpr int kMiso = 13;
static constexpr int kCs = 10;
static constexpr unsigned kWarmups = 10;
static constexpr unsigned kAttempts = 1000;
static constexpr size_t kMaxBytes = 1024;
static const size_t kSizes[] = {8, 16, 32, 64, 128, 256, 512, 1024};

// Static storage keeps the benchmark off the loop task's stack. The measured
// region contains only the HAL transfer and byte-for-byte verification.
static uint8_t s_tx[kMaxBytes];
static uint8_t s_rx[kMaxBytes];
static uint32_t s_latencies[kAttempts];
static char s_command[64];
static size_t s_command_len;

static void fill_pattern(size_t length, unsigned trial) {
    for (size_t i = 0; i < length; ++i) {
        s_tx[i] = (uint8_t)((i * 73u + trial * 29u + (i >> 3)) ^ 0xA5u);
        s_rx[i] = 0;
    }
}

static unsigned verify_bytes(size_t length) {
    unsigned mismatches = 0;
    for (size_t i = 0; i < length; ++i) {
        if (s_rx[i] != s_tx[i]) ++mismatches;
    }
    return mismatches;
}

static void print_samples(size_t length) {
    for (unsigned start = 0; start < kAttempts; start += 50) {
        printf("[SPIBENCH] SAMPLES bytes=%u start=%u values=",
               (unsigned)length, start);
        for (unsigned i = start; i < start + 50; ++i) {
            printf("%s%lu", i == start ? "" : ",", (unsigned long)s_latencies[i]);
        }
        putchar('\n');
    }
}

static void run_benchmark(unsigned mode, uint32_t hz) {
    if (mode > 3 || hz < 1000 || hz > 20000000) {
        printf("[SPIBENCH] ERROR invalid mode or clock\n");
        return;
    }

    int device = hal_spi_open("fspi", kSclk, kMosi, kMiso, kCs, hz, (uint8_t)mode);
    if (device < 0) {
        printf("[SPIBENCH] ERROR open=%d\n", device);
        return;
    }

    unsigned failed_sizes = 0;
    printf("[SPIBENCH] BEGIN mode=%u hz=%lu sclk=%d mosi=%d miso=%d cs=%d warmups=%u attempts=%u heap_before=%lu\n",
           mode, (unsigned long)hz, kSclk, kMosi, kMiso, kCs, kWarmups,
           kAttempts, (unsigned long)ESP.getFreeHeap());

    for (size_t size : kSizes) {
        unsigned warmup_errors = 0;
        for (unsigned i = 0; i < kWarmups; ++i) {
            fill_pattern(size, i);
            int result = hal_spi_transfer(device, s_tx, s_rx, size, 1000);
            if (result != (int)size || verify_bytes(size)) ++warmup_errors;
        }

        unsigned platform_errors = 0;
        unsigned timeouts = 0;
        unsigned failed_transactions = 0;
        unsigned mismatch_transactions = 0;
        unsigned mismatch_bytes = 0;
        uint64_t total_us = 0;
        uint32_t max_us = 0;
        for (unsigned i = 0; i < kAttempts; ++i) {
            fill_pattern(size, i + kWarmups);
            uint64_t start = hal_time_us();
            int result = hal_spi_transfer(device, s_tx, s_rx, size, 1000);
            uint64_t elapsed = hal_time_us() - start;
            s_latencies[i] = (uint32_t)elapsed;
            total_us += elapsed;
            if (elapsed > max_us) max_us = (uint32_t)elapsed;

            if (result != (int)size) {
                ++platform_errors;
                if (result == HAL_ERR_TIMEOUT) ++timeouts;
            }
            unsigned mismatches = verify_bytes(size);
            if (result != (int)size || mismatches) ++failed_transactions;
            if (mismatches) ++mismatch_transactions;
            mismatch_bytes += mismatches;
            if ((i & 31u) == 31u) yield();
        }

        if (warmup_errors || failed_transactions) ++failed_sizes;
        printf("[SPIBENCH] SIZE bytes=%u warmup_errors=%u attempts=%u failed_transactions=%u platform_errors=%u timeouts=%u mismatch_transactions=%u mismatch_bytes=%u total_us=%llu max_us=%lu heap_after=%lu\n",
               (unsigned)size, warmup_errors, kAttempts, failed_transactions,
               platform_errors, timeouts, mismatch_transactions, mismatch_bytes,
               (unsigned long long)total_us, (unsigned long)max_us,
               (unsigned long)ESP.getFreeHeap());
        print_samples(size);
    }

    int close_result = hal_spi_close(device);
    printf("[SPIBENCH] END failed_sizes=%u close=%d heap_after=%lu\n",
           failed_sizes, close_result, (unsigned long)ESP.getFreeHeap());
}

void setup() {
    platform_console_begin(3000);
    printf("[SPIBENCH] READY send BENCH <mode> <hz>\n");
}

void loop() {
    int ch;
    while ((ch = hal_console_getc(0)) >= 0) {
        if (ch == '\r') continue;
        if (ch != '\n') {
            if (s_command_len < sizeof(s_command) - 1)
                s_command[s_command_len++] = (char)ch;
            continue;
        }
        s_command[s_command_len] = 0;
        s_command_len = 0;
        unsigned mode;
        unsigned long hz;
        char extra;
        if (strcmp(s_command, "PING") == 0)
            printf("[SPIBENCH] READY send BENCH <mode> <hz>\n");
        else if (sscanf(s_command, "BENCH %u %lu %c", &mode, &hz, &extra) == 2)
            run_benchmark(mode, (uint32_t)hz);
        else
            printf("[SPIBENCH] ERROR expected BENCH <mode> <hz>\n");
    }
    delay(2);
}
