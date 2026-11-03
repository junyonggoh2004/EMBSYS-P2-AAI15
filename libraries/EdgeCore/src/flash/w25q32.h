#ifndef EDGECORE_FLASH_W25Q32_H
#define EDGECORE_FLASH_W25Q32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The caller owns the SPI device handle. No erase command is provided: a
 * scratch sector and erase policy must be approved before hardware writes. */
typedef struct {
    int spi_device;
    uint32_t jedec_id;
} w25q32_t;

/* expected_jedec_id is the 24-bit value confirmed from the actual part.
 * All-zero/all-FF, mismatched, and incomplete replies are rejected. */
int w25q32_probe(w25q32_t *flash, int spi_device, uint32_t expected_jedec_id);
int w25q32_read(const w25q32_t *flash, uint32_t address, uint8_t *out, size_t len);

/* Programs at most one 256-byte page, wholly inside [scratch_begin,
 * scratch_end). Requires the range to be blank, then checks WEL, WIP and
 * byte-for-byte readback. It never erases or wraps. */
int w25q32_program_verified(const w25q32_t *flash, uint32_t address,
                            const uint8_t *data, size_t len,
                            uint32_t scratch_begin, uint32_t scratch_end,
                            uint32_t deadline_ms);

#ifdef __cplusplus
}
#endif

#endif
