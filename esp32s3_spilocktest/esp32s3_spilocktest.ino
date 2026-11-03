// D2 HAL lock-contention check. Uses the production SPIClass HAL; MISO wiring
// is irrelevant because both transfers are TX-only. Restore esp32s3_edge after.
#include <EdgeS3.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_long_tx[1024];
static volatile bool s_started;
static volatile bool s_finished;
static volatile int s_worker_result;
static int s_device;
static char s_command[24];
static size_t s_command_len;

static void hold_spi(void *) {
    s_started = true;
    s_worker_result = hal_spi_transfer(s_device, s_long_tx, NULL,
                                       sizeof(s_long_tx), 1000);
    s_finished = true;
    vTaskDelete(NULL);
}

static void run_lock_test(void) {
    s_started = false;
    s_finished = false;
    s_worker_result = HAL_ERR_INVALID;
    s_device = hal_spi_open("fspi", 11, 12, 13, 10, 100000, 0);
    if (s_device < 0) {
        printf("[SPILOCK] ERROR open=%d\n", s_device);
        return;
    }
    BaseType_t created = xTaskCreatePinnedToCore(hold_spi, "spi-hold", 4096,
                                                  NULL, 2, NULL, 0);
    if (created != pdPASS) {
        printf("[SPILOCK] ERROR create=%ld\n", (long)created);
        hal_spi_close(s_device);
        return;
    }
    uint32_t deadline = hal_time_ms() + 1000;
    while (!s_started && (int32_t)(hal_time_ms() - deadline) < 0) delay(1);
    delay(2);  // allow the worker to acquire the SPI mutex

    uint8_t tx[2] = {0xA5, 0x5A};
    uint64_t start = hal_time_us();
    int contender = hal_spi_transfer(s_device, tx, NULL, sizeof(tx), 1);
    uint64_t contender_us = hal_time_us() - start;
    deadline = hal_time_ms() + 2000;
    while (!s_finished && (int32_t)(hal_time_ms() - deadline) < 0) delay(1);
    int worker = s_worker_result;
    bool finished = s_finished;
    int closed = finished ? hal_spi_close(s_device) : HAL_ERR_BUSY;
    printf("[SPILOCK] RESULT started=%d finished=%d worker=%d contender=%d contender_us=%llu close=%d\n",
           s_started ? 1 : 0, finished ? 1 : 0, worker, contender,
           (unsigned long long)contender_us, closed);
}

void setup() {
    platform_console_begin(3000);
    printf("[SPILOCK] READY\n");
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
        if (strcmp(s_command, "PING") == 0) printf("[SPILOCK] READY\n");
        else if (strcmp(s_command, "LOCKTEST") == 0) run_lock_test();
        else printf("[SPILOCK] ERROR command\n");
    }
    delay(2);
}
