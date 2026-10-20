// model_store_mtk.c
// mqtt_model_transfer.h on the ESP32-S3 with µT-Kernel (D1 task 31).
//
// Same topics, payloads and messages as the Pico and the Arduino build:
// pico/<node>/model/start {"size":N,"crc32":C}, model/chunk/<i>, model/end.
// The Arduino build kept the model in a LittleFS file; this build keeps two
// slots in the raw "model" partition (partitions.csv) and needs no file
// system or file-system locks:
//
//   slot = [4 KB header sector][model data, up to MODEL_FLASH_SIZE]
//   header = magic, sequence number, size, CRC32 of the data, CRC32 of the header
//
// An upload is written into the slot that is not in use. Its header is written
// last, and only after the data has been read back from flash and its size
// and CRC32 match the start message, so a corrupt, oversized or interrupted
// upload never touches the model in use. At boot the valid slot with the
// highest sequence number is used. The model is executed in place: the slot
// is mapped into the address space through the flash cache, so it is never
// copied into RAM. Data sectors are erased as the chunks reach them (about
// 45 ms each) rather than all at the start.
//
// Chunks arrive in the esp-mqtt task (net_mtk.c), the only writer of this
// state; SchedulerTask and InferenceTask only read the status and the
// pointer, which are published after the slot is complete. Flash writes stall
// the other core briefly (the course port parks it while the cache is off).
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_partition.h"

#include "mqtt/mqtt_model_transfer.h"

#define SLOT_HDR      0x1000u
#define SLOT_SIZE     (SLOT_HDR + MODEL_FLASH_SIZE)
#define SECTOR        0x1000u
#define HDR_MAGIC     0x314C444Du      // "MDL1"

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint32_t size;
    uint32_t crc;          // CRC32 of the model data
    uint32_t hdr_crc;      // CRC32 of the four words above
} slot_hdr_t;

static const esp_partition_t *s_part;
static volatile model_status_t s_status = MODEL_IDLE;
static const uint8_t *volatile s_model;  // mapped model in use, or NULL
static volatile uint32_t s_model_size;
static volatile int s_active = -1;        // newest complete slot (the MQTT task writes)
static uint32_t s_active_seq;
static volatile int s_lent = -1;          // slot last handed to the interpreter (SchedulerTask writes)
static esp_partition_mmap_handle_t s_map[2];
static bool s_mapped[2];

// upload in progress
static int s_target = -1;
static uint32_t s_expected_size, s_expected_crc, s_received, s_running_crc, s_erased_to;

// CRC32 as zlib computes it (send_model.py), chained across calls; the same
// function as the Arduino build.
static uint32_t crc32(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint32_t hdr_crc(const slot_hdr_t *h)
{
    return crc32(0, (const uint8_t *)h, offsetof(slot_hdr_t, hdr_crc));
}

static bool read_hdr(int slot, slot_hdr_t *h)
{
    if (esp_partition_read(s_part, (size_t)slot * SLOT_SIZE, h, sizeof *h) != ESP_OK) return false;
    return h->magic == HDR_MAGIC && h->hdr_crc == hdr_crc(h) && h->size > 0 && h->size <= MODEL_FLASH_SIZE;
}

// CRC32 of the data as stored in flash, read back in 1 KB pieces.
static uint32_t flash_crc(int slot, uint32_t size)
{
    static uint8_t buf[1024];
    uint32_t crc = 0;
    for (uint32_t off = 0; off < size; off += sizeof buf) {
        uint32_t n = size - off < sizeof buf ? size - off : sizeof buf;
        if (esp_partition_read(s_part, (size_t)slot * SLOT_SIZE + SLOT_HDR + off, buf, n) != ESP_OK) return ~crc;
        crc = crc32(crc, buf, n);
    }
    return crc;
}

// Maps a slot's data and makes it the model in use.
static bool use_slot(int slot, uint32_t size, uint32_t seq)
{
    const void *p = NULL;
    esp_partition_mmap_handle_t h;
    if (esp_partition_mmap(s_part, (size_t)slot * SLOT_SIZE + SLOT_HDR, size, ESP_PARTITION_MMAP_DATA, &p, &h) != ESP_OK)
        return false;
    // The other slot's mapping stays until that slot is rewritten: the
    // interpreter may still be running the previous model.
    if (s_mapped[slot]) esp_partition_munmap(s_map[slot]);
    s_map[slot] = h;
    s_mapped[slot] = true;
    s_model_size = size;
    s_model = (const uint8_t *)p;
    s_active = slot;
    s_active_seq = seq;
    s_status = MODEL_READY;
    return true;
}

// After a failed upload, fall back to the previous model if there is one.
static void fail(void)
{
    s_target = -1;
    if (s_model) {
        s_status = MODEL_READY;
        printf("[MODEL] Previous model kept (%u bytes)\n", (unsigned)s_model_size);
    } else {
        s_status = MODEL_ERROR;
    }
}

static bool topic_ends_with(const char *topic, const char *suffix)
{
    const char *p = strstr(topic, "/model/");
    return p && strcmp(p + 7, suffix) == 0;
}

static int topic_chunk_index(const char *topic)
{
    const char *p = strstr(topic, "/model/chunk/");
    return p ? atoi(p + 13) : -1;
}

static void handle_start(const uint8_t *data, size_t len)
{
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
    if (!s_part) {
        printf("[MODEL] No model partition: upload refused\n");
        fail();
        return;
    }
    if (s_status == MODEL_RECEIVING) printf("[MODEL] Previous upload interrupted; restarting\n");
    int slot = s_active == 0 ? 1 : 0;      // never the newest complete slot
    // Nor the slot the interpreter is running: that happens only when an
    // upload completed and another started before the first was loaded.
    if (slot == s_lent) {
        printf("[MODEL] Upload refused: the previous upload has not been loaded yet; retry in a second\n");
        fail();
        return;
    }
    // Erasing the header sector invalidates the slot before any data changes.
    if (s_mapped[slot]) { esp_partition_munmap(s_map[slot]); s_mapped[slot] = false; }
    if (esp_partition_erase_range(s_part, (size_t)slot * SLOT_SIZE, SECTOR) != ESP_OK) {
        printf("[MODEL] Flash erase failed\n");
        fail();
        return;
    }
    printf("[MODEL] Transfer starting: %u bytes, CRC32=0x%08X\n",
           (unsigned)s_expected_size, (unsigned)s_expected_crc);
    s_target = slot;
    s_received = 0;
    s_running_crc = 0;
    s_erased_to = 0;
    s_status = MODEL_RECEIVING;
}

static void handle_chunk(int idx, const uint8_t *data, size_t len)
{
    if (s_status != MODEL_RECEIVING || s_target < 0) {
        printf("[MODEL] Chunk %d ignored (not receiving)\n", idx);
        return;
    }
    if (s_received + len > s_expected_size) {
        printf("[MODEL] Chunk %d overflows expected size\n", idx);
        fail();
        return;
    }
    size_t base = (size_t)s_target * SLOT_SIZE + SLOT_HDR;
    while (s_erased_to < s_received + len) {   // erase the sectors this chunk reaches
        if (esp_partition_erase_range(s_part, base + s_erased_to, SECTOR) != ESP_OK) {
            printf("[MODEL] Flash erase failed at chunk %d\n", idx);
            fail();
            return;
        }
        s_erased_to += SECTOR;
    }
    if (esp_partition_write(s_part, base + s_received, data, len) != ESP_OK) {
        printf("[MODEL] Write failed at chunk %d\n", idx);
        fail();
        return;
    }
    s_running_crc = crc32(s_running_crc, data, len);
    s_received += (uint32_t)len;
    printf("[MODEL] Chunk %d: %u / %u bytes (%u%%)\n", idx, (unsigned)s_received,
           (unsigned)s_expected_size, (unsigned)(100ULL * s_received / s_expected_size));
}

static void handle_end(void)
{
    if (s_status != MODEL_RECEIVING || s_target < 0) {
        printf("[MODEL] End ignored (not receiving)\n");
        return;
    }
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
    uint32_t stored = flash_crc(s_target, s_received);
    if (stored != s_expected_crc) {
        printf("[MODEL] Flash read-back CRC32 0x%08X does not match: upload refused\n", (unsigned)stored);
        fail();
        return;
    }
    slot_hdr_t h = { HDR_MAGIC, s_active_seq + 1, s_received, s_expected_crc, 0 };
    h.hdr_crc = hdr_crc(&h);
    int slot = s_target;
    if (esp_partition_write(s_part, (size_t)slot * SLOT_SIZE, &h, sizeof h) != ESP_OK || !use_slot(slot, h.size, h.seq)) {
        printf("[MODEL] Could not save the model\n");
        fail();
        return;
    }
    s_target = -1;
    printf("[MODEL] Transfer complete! %u bytes verified, saved to flash slot %d\n", (unsigned)s_model_size, slot);
}

void model_transfer_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "model");
    printf("[MODEL] Model transfer subsystem initialised.\n");
    if (!s_part || s_part->size < 2 * SLOT_SIZE) {
        printf("[MODEL] No model partition of %u bytes: uploads disabled\n", (unsigned)(2 * SLOT_SIZE));
        s_part = NULL;
        return;
    }
    int best = -1;
    slot_hdr_t hb = { 0 };
    for (int slot = 0; slot < 2; slot++) {
        slot_hdr_t h;
        if (!read_hdr(slot, &h)) continue;
        if (flash_crc(slot, h.size) != h.crc) {
            printf("[MODEL] Slot %d fails its CRC32 check; ignored\n", slot);
            continue;
        }
        if (best < 0 || h.seq > hb.seq) { best = slot; hb = h; }
    }
    if (best >= 0 && use_slot(best, hb.size, hb.seq))
        printf("[MODEL] Loaded saved model: %u bytes from flash slot %d\n", (unsigned)s_model_size, best);
    else
        printf("[MODEL] No saved model; using the built-in one\n");
}

void model_transfer_on_data(const char *topic, const uint8_t *data, size_t len)
{
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

void model_transfer_poll(void) {}

model_status_t model_transfer_status(void) { return s_status; }

uint32_t model_transfer_model_size(void) { return s_model_size; }

// Called by the inference manager (SchedulerTask) to load the model: the
// slot it returns is then in use by the interpreter until another is lent.
const uint8_t *model_transfer_get_model_ptr(void)
{
    if (s_status != MODEL_READY || s_model_size == 0) return NULL;
    s_lent = s_active;
    return s_model;
}
