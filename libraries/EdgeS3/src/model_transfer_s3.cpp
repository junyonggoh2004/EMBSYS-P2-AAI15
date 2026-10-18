// model_transfer_s3.cpp — mqtt_model_transfer.h on the ESP32-S3 (task 20)
//
// Same topics, payloads and messages as the Pico (project_v3/mqtt/
// mqtt_model_transfer.c): pico/<node>/model/start {"size":N,"crc32":C},
// model/chunk/<i>, model/end. The Pico writes raw flash at a fixed offset;
// here the model goes to LittleFS on the data partition of the Huge APP
// scheme. Chunks are written to a temporary file, which replaces the saved
// model only after the size and CRC32 check, so a corrupt, oversized or
// interrupted upload leaves the previous model in use. The saved model is
// read into PSRAM at boot, so it survives a reboot.
//
// Chunks arrive in MQTTTask; SchedulerTask and InferenceTask only read the
// status and pointer, which are published after the buffer is complete.

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "mqtt/mqtt_model_transfer.h"
}

static const char *MODEL_PATH = "/model.tflite";
static const char *TEMP_PATH  = "/model.part";

static volatile model_status_t s_status = MODEL_IDLE;
static const uint8_t *volatile s_model = nullptr;   // last good model, PSRAM
static volatile uint32_t s_model_size  = 0;

static bool     s_fs_ok = false;
static File     s_temp;
static uint32_t s_expected_size = 0;
static uint32_t s_expected_crc  = 0;
static uint32_t s_received      = 0;
static uint32_t s_running_crc   = 0;

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
    }
    return ~crc;
}

// Reads the saved model into a new buffer. Older buffers are not freed: the
// interpreter may still hold a pointer into one.
static bool load_saved_model(void) {
    File f = LittleFS.open(MODEL_PATH, "r");
    if (!f) return false;
    size_t n = f.size();
    uint8_t *buf = (n > 0 && n <= MODEL_FLASH_SIZE) ? (uint8_t *)malloc(n) : nullptr;
    bool ok = buf && f.read(buf, n) == n;
    f.close();
    if (!ok) { free(buf); return false; }
    s_model_size = n;
    s_model = buf;
    s_status = MODEL_READY;
    return true;
}

// After a failed upload, fall back to the previous model if there is one.
static void fail(void) {
    if (s_temp) s_temp.close();
    if (s_fs_ok) LittleFS.remove(TEMP_PATH);
    if (s_model) {
        s_status = MODEL_READY;
        printf("[MODEL] Previous model kept (%u bytes)\n", (unsigned)s_model_size);
    } else {
        s_status = MODEL_ERROR;
    }
}

static bool topic_ends_with(const char *topic, const char *suffix) {
    const char *p = strstr(topic, "/model/");
    return p && strcmp(p + 7, suffix) == 0;
}

static int topic_chunk_index(const char *topic) {
    const char *p = strstr(topic, "/model/chunk/");
    return p ? atoi(p + 13) : -1;
}

static void handle_start(const uint8_t *data, size_t len) {
    char buf[128];
    size_t n = len < sizeof buf - 1 ? len : sizeof buf - 1;
    memcpy(buf, data, n);
    buf[n] = 0;
    const char *ps = strstr(buf, "\"size\":");
    const char *pc = strstr(buf, "\"crc32\":");
    if (!ps || !pc) {
        printf("[MODEL] Invalid start payload: %s\n", buf);
        fail();
        return;
    }
    s_expected_size = (uint32_t)strtoul(ps + 7, NULL, 10);
    s_expected_crc  = (uint32_t)strtoul(pc + 8, NULL, 10);
    if (s_expected_size == 0 || s_expected_size > MODEL_FLASH_SIZE) {
        printf("[MODEL] Invalid size %u (max %u)\n", (unsigned)s_expected_size, (unsigned)MODEL_FLASH_SIZE);
        fail();
        return;
    }
    if (!s_fs_ok) {
        printf("[MODEL] No file system: upload refused\n");
        fail();
        return;
    }
    if (s_status == MODEL_RECEIVING) printf("[MODEL] Previous upload interrupted; restarting\n");
    if (s_temp) s_temp.close();
    s_temp = LittleFS.open(TEMP_PATH, "w");
    if (!s_temp) {
        printf("[MODEL] Cannot open %s\n", TEMP_PATH);
        fail();
        return;
    }
    printf("[MODEL] Transfer starting: %u bytes, CRC32=0x%08X\n",
           (unsigned)s_expected_size, (unsigned)s_expected_crc);
    s_received = 0;
    s_running_crc = 0;
    s_status = MODEL_RECEIVING;
}

static void handle_chunk(int idx, const uint8_t *data, size_t len) {
    if (s_status != MODEL_RECEIVING) {
        printf("[MODEL] Chunk %d ignored (not receiving)\n", idx);
        return;
    }
    if (s_received + len > s_expected_size) {
        printf("[MODEL] Chunk %d overflows expected size\n", idx);
        fail();
        return;
    }
    if (s_temp.write(data, len) != len) {
        printf("[MODEL] Write failed at chunk %d\n", idx);
        fail();
        return;
    }
    s_running_crc = crc32_update(s_running_crc, data, len);
    s_received += len;
    printf("[MODEL] Chunk %d: %u / %u bytes (%.0f%%)\n", idx, (unsigned)s_received,
           (unsigned)s_expected_size, 100.0 * s_received / s_expected_size);
}

static void handle_end(void) {
    if (s_status != MODEL_RECEIVING) {
        printf("[MODEL] End ignored (not receiving)\n");
        return;
    }
    s_temp.close();
    if (s_received != s_expected_size) {
        printf("[MODEL] Size mismatch: got %u, expected %u\n", (unsigned)s_received, (unsigned)s_expected_size);
        fail();
        return;
    }
    if (s_running_crc != s_expected_crc) {
        printf("[MODEL] CRC32 mismatch: got 0x%08X, expected 0x%08X\n",
               (unsigned)s_running_crc, (unsigned)s_expected_crc);
        fail();
        return;
    }
    LittleFS.remove(MODEL_PATH);
    if (!LittleFS.rename(TEMP_PATH, MODEL_PATH) || !load_saved_model()) {
        printf("[MODEL] Could not save the model\n");
        fail();
        return;
    }
    printf("[MODEL] Transfer complete! %u bytes verified, saved to %s\n", (unsigned)s_model_size, MODEL_PATH);
}

extern "C" void model_transfer_init(void) {
    s_fs_ok = LittleFS.begin(true);   // formats the data partition on first use
    printf("[MODEL] Model transfer subsystem initialised.\n");
    if (!s_fs_ok) {
        printf("[MODEL] LittleFS mount failed: uploads disabled\n");
        return;
    }
    LittleFS.remove(TEMP_PATH);       // left over from an upload cut by a reset
    if (load_saved_model())
        printf("[MODEL] Loaded saved model: %u bytes from %s\n", (unsigned)s_model_size, MODEL_PATH);
    else
        printf("[MODEL] No saved model; using the built-in one\n");
}

extern "C" void model_transfer_on_data(const char *topic, const uint8_t *data, size_t len) {
    if (topic_ends_with(topic, "start")) {
        handle_start(data, len);
    } else if (topic_ends_with(topic, "end")) {
        handle_end();
    } else {
        int idx = topic_chunk_index(topic);
        if (idx >= 0) handle_chunk(idx, data, len);
        else printf("[MODEL] Unknown model topic: %s\n", topic);
    }
}

extern "C" void model_transfer_poll(void) {}

extern "C" model_status_t model_transfer_status(void) { return s_status; }

extern "C" uint32_t model_transfer_model_size(void) { return s_model_size; }

extern "C" const uint8_t *model_transfer_get_model_ptr(void) {
    return (s_status == MODEL_READY && s_model_size > 0) ? s_model : nullptr;
}
