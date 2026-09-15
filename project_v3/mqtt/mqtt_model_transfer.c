/* mqtt_model_transfer.c — MQTT OTA model transfer to Flash
 *
 * Protocol:
 *   1. Server publishes JSON to  pico/<node>/model/start
 *      Payload: {"size":<total_bytes>, "crc32":<expected_crc>}
 *   2. Server sends binary chunks to  pico/<node>/model/chunk/<index>
 *      Each chunk is raw binary, max MODEL_CHUNK_MAX bytes.
 *   3. Server publishes to  pico/<node>/model/end
 *      Pico verifies CRC32, marks model as READY.
 */

#include "mqtt/mqtt_model_transfer.h"
#include "mqtt/mqtt_telemetry.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

/* ---- State ---- */
static model_status_t s_status = MODEL_IDLE;
static uint32_t s_expected_size = 0;
static uint32_t s_expected_crc  = 0;
static uint32_t s_received      = 0;    /* bytes written so far */
static uint32_t s_model_size    = 0;    /* size of last valid model */

/* Align writes to FLASH_PAGE_SIZE (256 bytes on RP2350).
 * We accumulate a page-aligned buffer and flush when full. */
static uint8_t  s_page_buf[FLASH_PAGE_SIZE];
static size_t   s_page_used = 0;

/* ---- Simple CRC32 (matches standard CRC-32/ISO 3309) ---- */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

static uint32_t s_running_crc = 0;

/* ---- Flash helpers ---- */

/* Erase the entire model region.
 * flash_range_erase works in FLASH_SECTOR_SIZE (4096 byte) units. */
static void erase_model_region(void) {
    printf("[MODEL] Erasing %u KB at offset 0x%08X ...\n",
           MODEL_FLASH_SIZE / 1024, MODEL_FLASH_OFFSET);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(MODEL_FLASH_OFFSET, MODEL_FLASH_SIZE);
    restore_interrupts(ints);

    printf("[MODEL] Erase complete.\n");
}

/* Write one page (256 bytes) to flash at current offset. */
static bool flash_write_page(uint32_t offset, const uint8_t *data) {
    if (offset + FLASH_PAGE_SIZE > MODEL_FLASH_OFFSET + MODEL_FLASH_SIZE) {
        printf("[MODEL] Flash write out of bounds at offset 0x%08X\n", offset);
        return false;
    }
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(offset, data, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    return true;
}

/* Flush any remaining bytes in the page buffer (pad with 0xFF). */
static bool flush_page_buf(void) {
    if (s_page_used == 0) return true;

    /* Pad remainder with 0xFF (erased flash value) */
    memset(s_page_buf + s_page_used, 0xFF, FLASH_PAGE_SIZE - s_page_used);

    uint32_t page_offset = MODEL_FLASH_OFFSET +
                           (s_received - s_page_used);    /* start of this page */
    /* Align down to page boundary */
    page_offset = page_offset & ~((uint32_t)(FLASH_PAGE_SIZE - 1));

    bool ok = flash_write_page(page_offset, s_page_buf);
    s_page_used = 0;
    return ok;
}

/* ---- Topic parsing helpers ---- */

/* Check if topic matches pico/<node>/model/<suffix> */
static bool topic_ends_with(const char *topic, const char *suffix) {
    const char *p = strstr(topic, "/model/");
    if (!p) return false;
    p += 7; /* skip "/model/" */
    return strcmp(p, suffix) == 0;
}

/* Extract chunk index from pico/<node>/model/chunk/<index> */
static int topic_chunk_index(const char *topic) {
    const char *p = strstr(topic, "/model/chunk/");
    if (!p) return -1;
    p += 13; /* skip "/model/chunk/" */
    return atoi(p);
}

/* ---- Handle incoming model/start ---- */
static void handle_start(const uint8_t *data, size_t len) {
    /* Parse simple JSON: {"size":12345,"crc32":2863311530} */
    char buf[128];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, data, n);
    buf[n] = '\0';

    const char *ps = strstr(buf, "\"size\":");
    const char *pc = strstr(buf, "\"crc32\":");

    if (!ps || !pc) {
        printf("[MODEL] Invalid start payload: %s\n", buf);
        s_status = MODEL_ERROR;
        return;
    }

    s_expected_size = (uint32_t)strtoul(ps + 7, NULL, 10);
    s_expected_crc  = (uint32_t)strtoul(pc + 8, NULL, 10);

    if (s_expected_size == 0 || s_expected_size > MODEL_FLASH_SIZE) {
        printf("[MODEL] Invalid size %u (max %u)\n",
               s_expected_size, MODEL_FLASH_SIZE);
        s_status = MODEL_ERROR;
        return;
    }

    printf("[MODEL] Transfer starting: %u bytes, CRC32=0x%08X\n",
           s_expected_size, s_expected_crc);

    /* Erase the target flash region */
    erase_model_region();

    s_received    = 0;
    s_running_crc = 0;
    s_page_used   = 0;
    s_status      = MODEL_RECEIVING;
}

/* ---- Handle incoming model/chunk/<index> ---- */
static void handle_chunk(int chunk_idx, const uint8_t *data, size_t len) {
    if (s_status != MODEL_RECEIVING) {
        printf("[MODEL] Chunk %d ignored (not receiving)\n", chunk_idx);
        return;
    }

    if (s_received + len > s_expected_size) {
        printf("[MODEL] Chunk %d overflows expected size\n", chunk_idx);
        s_status = MODEL_ERROR;
        return;
    }

    /* Update CRC */
    s_running_crc = crc32_update(s_running_crc, data, len);

    /* Accumulate into page buffer and write full pages */
    size_t src_off = 0;
    while (src_off < len) {
        size_t space = FLASH_PAGE_SIZE - s_page_used;
        size_t copy  = (len - src_off) < space ? (len - src_off) : space;

        memcpy(s_page_buf + s_page_used, data + src_off, copy);
        s_page_used += copy;
        src_off     += copy;
        s_received  += copy;

        if (s_page_used == FLASH_PAGE_SIZE) {
            uint32_t page_off = MODEL_FLASH_OFFSET +
                                (s_received - FLASH_PAGE_SIZE);
            if (!flash_write_page(page_off, s_page_buf)) {
                s_status = MODEL_ERROR;
                return;
            }
            s_page_used = 0;
        }
    }

    if ((chunk_idx & 0x0F) == 0) {  /* print every 16 chunks */
        printf("[MODEL] Chunk %d: %u / %u bytes (%.0f%%)\n",
               chunk_idx, s_received, s_expected_size,
               100.0f * s_received / s_expected_size);
    }
}

/* ---- Handle incoming model/end ---- */
static void handle_end(void) {
    if (s_status != MODEL_RECEIVING) {
        printf("[MODEL] End ignored (not receiving)\n");
        return;
    }

    /* Flush any remaining partial page */
    if (!flush_page_buf()) {
        s_status = MODEL_ERROR;
        return;
    }

    if (s_received != s_expected_size) {
        printf("[MODEL] Size mismatch: got %u, expected %u\n",
               s_received, s_expected_size);
        s_status = MODEL_ERROR;
        return;
    }

    if (s_running_crc != s_expected_crc) {
        printf("[MODEL] CRC32 mismatch: got 0x%08X, expected 0x%08X\n",
               s_running_crc, s_expected_crc);
        s_status = MODEL_ERROR;
        return;
    }

    s_model_size = s_expected_size;
    s_status = MODEL_READY;
    printf("[MODEL] Transfer complete! %u bytes verified at 0x%08X\n",
           s_model_size, MODEL_FLASH_ADDR);
}

/* ---- Public API ---- */

void model_transfer_init(void) {
    s_status = MODEL_IDLE;
    s_model_size = 0;
    printf("[MODEL] Model transfer subsystem initialised.\n");
    printf("[MODEL] Flash region: offset=0x%08X, addr=0x%08X, size=%uKB\n",
           MODEL_FLASH_OFFSET, MODEL_FLASH_ADDR, MODEL_FLASH_SIZE / 1024);
}

void model_transfer_on_data(const char *topic,
                            const uint8_t *data, size_t len) {
    if (topic_ends_with(topic, "start")) {
        handle_start(data, len);
    } else if (topic_ends_with(topic, "end")) {
        handle_end();
    } else {
        int idx = topic_chunk_index(topic);
        if (idx >= 0) {
            handle_chunk(idx, data, len);
        } else {
            printf("[MODEL] Unknown model topic: %s\n", topic);
        }
    }
}

void model_transfer_poll(void) {
    /* Currently nothing deferred — flash writes happen inline.
     * This hook exists for future async/Core1 offloading. */
}

model_status_t model_transfer_status(void) {
    return s_status;
}

uint32_t model_transfer_model_size(void) {
    return s_model_size;
}

const uint8_t *model_transfer_get_model_ptr(void) {
    if (s_status == MODEL_READY && s_model_size > 0) {
        return (const uint8_t *)MODEL_FLASH_ADDR;
    }
    return NULL;
}
