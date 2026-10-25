#include "flash/w25q32.h"

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
