#include "flash/w25q32.h"
#include "flash/d2_event_log.h"

#include <string.h>

#include "hal/hal.h"

#define FLASH_BYTES (1u << 22)
#define PAGE_BYTES 256u
#define SPI_TIMEOUT_MS 100u

static int transfer(const w25q32_t *flash, const uint8_t *tx, uint8_t *rx, size_t len)
{
    int n = hal_spi_transfer(flash->spi_device, tx, rx, len, SPI_TIMEOUT_MS);
    return n < 0 ? n : ((size_t)n == len ? HAL_OK : HAL_ERR_IO);
}

static int status1(const w25q32_t *flash, uint8_t *status)
{
    const uint8_t tx[2] = { 0x05, 0x00 };
    uint8_t rx[2] = { 0 };
    int rc = transfer(flash, tx, rx, sizeof tx);
    if (rc == HAL_OK) *status = rx[1];
    return rc;
}

int w25q32_probe(w25q32_t *flash, int spi_device, uint32_t expected_jedec_id)
{
    if (!flash || spi_device < 0 || expected_jedec_id == 0 ||
        expected_jedec_id > 0xffffffu || expected_jedec_id == 0xffffffu)
        return HAL_ERR_INVALID;
    *flash = (w25q32_t){ .spi_device = spi_device, .jedec_id = 0 };
    const uint8_t tx[4] = { 0x9f, 0, 0, 0 };
    uint8_t rx[4] = { 0 };
    int rc = transfer(flash, tx, rx, sizeof tx);
    if (rc != HAL_OK) return rc;
    uint32_t id = ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
    if (id == 0 || id == 0xffffffu) return HAL_ERR_ABSENT;
    if (id != expected_jedec_id) return HAL_ERR_VERIFY;
    flash->jedec_id = id;
    return HAL_OK;
}

int w25q32_read(const w25q32_t *flash, uint32_t address, uint8_t *out, size_t len)
{
    if (!flash || !flash->jedec_id || !out || !len || len > PAGE_BYTES ||
        address >= FLASH_BYTES || len > FLASH_BYTES - address)
        return HAL_ERR_INVALID;
    uint8_t tx[PAGE_BYTES + 4] = { 0x03, (uint8_t)(address >> 16),
                                   (uint8_t)(address >> 8), (uint8_t)address };
    uint8_t rx[PAGE_BYTES + 4] = { 0 };
    int rc = transfer(flash, tx, rx, len + 4);
    if (rc == HAL_OK) memcpy(out, rx + 4, len);
    return rc;
}

int w25q32_program_verified(const w25q32_t *flash, uint32_t address,
                            const uint8_t *data, size_t len,
                            uint32_t scratch_begin, uint32_t scratch_end,
                            uint32_t deadline_ms)
{
    if (!flash || !flash->jedec_id || !data || !len || len > PAGE_BYTES ||
        !deadline_ms || deadline_ms > 1000 || scratch_begin >= scratch_end ||
        scratch_end > FLASH_BYTES || address < scratch_begin ||
        address >= scratch_end || len > scratch_end - address ||
        len > PAGE_BYTES - (address & (PAGE_BYTES - 1)))
        return HAL_ERR_INVALID;

    uint8_t check[PAGE_BYTES];
    int rc = w25q32_read(flash, address, check, len);
    if (rc != HAL_OK) return rc;
    for (size_t i = 0; i < len; ++i)
        if (check[i] != 0xff) return HAL_ERR_BUSY; // never overwrite unknown data

    const uint8_t enable = 0x06;
    rc = transfer(flash, &enable, NULL, 1);
    if (rc != HAL_OK) return rc;
    uint8_t status;
    rc = status1(flash, &status);
    if (rc != HAL_OK) return rc;
    if (!(status & 0x02) || (status & 0x01)) return HAL_ERR_IO;

    uint8_t tx[PAGE_BYTES + 4] = { 0x02, (uint8_t)(address >> 16),
                                   (uint8_t)(address >> 8), (uint8_t)address };
    memcpy(tx + 4, data, len);
    rc = transfer(flash, tx, NULL, len + 4);
    if (rc != HAL_OK) return rc;

    uint64_t end_us = hal_time_us() + (uint64_t)deadline_ms * 1000;
    do {
        rc = status1(flash, &status);
        if (rc != HAL_OK) return rc;
        if (!(status & 0x01)) break;
        hal_delay_ms(1);
    } while (hal_time_us() < end_us);
    if (status & 0x01) return HAL_ERR_TIMEOUT;

    rc = w25q32_read(flash, address, check, len);
    if (rc != HAL_OK) return rc;
    if (memcmp(check, data, len) != 0) return HAL_ERR_VERIFY;
    w25q32_t confirmed;
    return w25q32_probe(&confirmed, flash->spi_device, flash->jedec_id);
}

/* D2 event-log implementation lives beside the D2-owned device layer so both
 * existing Pico and ESP32-S3 manifests compile it without edits to team-owned
 * build/runtime files. It can be split into its own translation unit when the
 * application owners integrate the handoff. */

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* CRC-32/ISO-HDLC: reflected 0x04C11DB7, init/final XOR 0xFFFFFFFF. */
static uint32_t log_crc(const uint8_t *p, size_t len)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}

static int record_valid(const d2_log_record_t *r)
{
    if (!r || !r->node_id || !r->boot_id || !r->log_seq ||
        r->result_code > D2_LOG_REJECTED || r->decision_source > 2 ||
        r->decided_class > 4 || r->publish_status > D2_LOG_PUBLISH_FAILED ||
        r->event_valid > 1 || r->degraded > 1 ||
        (r->confidence_bp > 10000 && r->confidence_bp != UINT16_MAX))
        return HAL_ERR_INVALID;
    if (r->result_code == D2_LOG_APPLIED &&
        (!r->event_valid || !r->event_id || !r->source_id ||
         !r->source_seq || !r->window_start_us ||
         r->window_end_us < r->window_start_us))
        return HAL_ERR_INVALID;
    return HAL_OK;
}

int d2_log_encode(const d2_log_record_t *r, uint8_t out[D2_LOG_WIRE_BYTES])
{
    if (!out || record_valid(r) != HAL_OK) return HAL_ERR_INVALID;
    memset(out, 0, D2_LOG_WIRE_BYTES);
    memcpy(out, "D2LG", 4);
    out[4] = D2_LOG_VERSION;
    out[5] = D2_LOG_WIRE_BYTES;
    out[6] = r->degraded;
    out[7] = r->publish_status;
    put64(out + 8, r->node_id);
    put32(out + 16, r->boot_id);
    put32(out + 20, r->log_seq);
    put32(out + 24, r->event_id);
    put32(out + 28, r->source_id);
    put32(out + 32, r->source_seq);
    put64(out + 36, r->window_start_us);
    put64(out + 44, r->window_end_us);
    put32(out + 52, r->quality_flags);
    put32(out + 56, r->model_version);
    put32(out + 60, (uint32_t)r->occupancy_before);
    put32(out + 64, (uint32_t)r->occupancy_after);
    put16(out + 68, r->confidence_bp);
    out[70] = r->result_code;
    out[71] = r->decision_source;
    out[72] = r->decided_class;
    out[73] = r->reason_code;
    out[74] = r->event_valid;
    put32(out + 76, log_crc(out, 76));
    return HAL_OK;
}

int d2_log_decode(const uint8_t in[D2_LOG_WIRE_BYTES], d2_log_record_t *r)
{
    if (!in || !r) return HAL_ERR_INVALID;
    if (memcmp(in, "D2LG", 4) != 0 || in[4] != D2_LOG_VERSION ||
        in[5] != D2_LOG_WIRE_BYTES || in[75] != 0 ||
        get32(in + 76) != log_crc(in, 76))
        return HAL_ERR_VERIFY;
    d2_log_record_t decoded = {
        .node_id = get64(in + 8),
        .boot_id = get32(in + 16),
        .log_seq = get32(in + 20),
        .event_id = get32(in + 24),
        .source_id = get32(in + 28),
        .source_seq = get32(in + 32),
        .window_start_us = get64(in + 36),
        .window_end_us = get64(in + 44),
        .quality_flags = get32(in + 52),
        .model_version = get32(in + 56),
        .occupancy_before = (int32_t)get32(in + 60),
        .occupancy_after = (int32_t)get32(in + 64),
        .confidence_bp = get16(in + 68),
        .result_code = in[70],
        .decision_source = in[71],
        .decided_class = in[72],
        .reason_code = in[73],
        .event_valid = in[74],
        .degraded = in[6],
        .publish_status = in[7]
    };
    if (record_valid(&decoded) != HAL_OK) return HAL_ERR_VERIFY;
    *r = decoded;
    return HAL_OK;
}

int d2_log_open(d2_event_log_t *log, const w25q32_t *flash,
                uint32_t approved_begin, uint32_t approved_end)
{
    if (!log || !flash || !flash->jedec_id ||
        approved_begin >= approved_end || approved_end > FLASH_BYTES ||
        approved_end - approved_begin > D2_LOG_MAX_REGION_BYTES ||
        (approved_begin >> 12) != ((approved_end - 1u) >> 12) ||
        (approved_begin & (D2_LOG_SLOT_BYTES - 1u)) ||
        (approved_end & (D2_LOG_SLOT_BYTES - 1u)))
        return HAL_ERR_INVALID;
    d2_event_log_t found = {
        .flash = flash, .begin = approved_begin, .end = approved_end,
        .next = approved_begin,
        .slots = (approved_end - approved_begin) / D2_LOG_SLOT_BYTES
    };
    uint8_t slot[D2_LOG_SLOT_BYTES];
    for (uint32_t address = approved_begin; address < approved_end;
         address += D2_LOG_SLOT_BYTES) {
        int rc = w25q32_read(flash, address, slot, sizeof slot);
        if (rc != HAL_OK) return rc;
        bool blank = true;
        for (size_t i = 0; i < sizeof slot; ++i)
            if (slot[i] != 0xffu) { blank = false; break; }
        if (blank) continue;
        found.next = address + D2_LOG_SLOT_BYTES;
        d2_log_record_t record;
        if (d2_log_decode(slot, &record) == HAL_OK) ++found.valid;
        else ++found.invalid;
    }
    *log = found;
    return HAL_OK;
}

int d2_log_append(d2_event_log_t *log, const d2_log_record_t *record,
                  uint32_t deadline_ms)
{
    if (!log || !log->flash || !deadline_ms ||
        log->next < log->begin || log->next > log->end)
        return HAL_ERR_INVALID;
    if (log->next == log->end) return D2_LOG_ERR_FULL;
    uint8_t encoded[D2_LOG_WIRE_BYTES];
    int rc = d2_log_encode(record, encoded);
    if (rc != HAL_OK) return rc;
    uint8_t slot[D2_LOG_SLOT_BYTES];
    rc = w25q32_read(log->flash, log->next, slot, sizeof slot);
    if (rc != HAL_OK) return rc;
    for (size_t i = 0; i < sizeof slot; ++i)
        if (slot[i] != 0xffu) return HAL_ERR_BUSY;
    uint32_t address = log->next;
    /* Advance even if the program fails: a timeout can leave a torn page. */
    log->next += D2_LOG_SLOT_BYTES;
    rc = w25q32_program_verified(log->flash, address, encoded,
                                 sizeof encoded, log->begin, log->end,
                                 deadline_ms);
    if (rc == HAL_OK) ++log->valid;
    else ++log->write_errors;
    return rc;
}

int d2_log_read(const d2_event_log_t *log, uint32_t slot,
                d2_log_record_t *record)
{
    if (!log || !log->flash || !record || slot >= log->slots)
        return HAL_ERR_INVALID;
    uint8_t encoded[D2_LOG_WIRE_BYTES];
    int rc = w25q32_read(log->flash, log->begin + slot * D2_LOG_SLOT_BYTES,
                          encoded, sizeof encoded);
    return rc == HAL_OK ? d2_log_decode(encoded, record) : rc;
}
