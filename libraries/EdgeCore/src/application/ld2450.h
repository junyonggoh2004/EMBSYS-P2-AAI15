#ifndef APPLICATION_LD2450_H
#define APPLICATION_LD2450_H
#include <stdint.h>
#include <stddef.h>
/* HLK LD2450 serial protocol v1.03: AA FF 03 00 + 3*8 + 55 CC.
 * No checksum exists in this report format; framing is not integrity proof. */
typedef struct { int32_t xmin, xmax, ymin, ymax; } ld2450_zone_t;
typedef struct {
    uint8_t bytes[30];
    unsigned used;
    uint32_t frames, malformed;
} ld2450_decoder_t;
typedef void (*ld2450_report_t)(unsigned targets, uint64_t receipt_us, void *);
void ld2450_feed(ld2450_decoder_t *, const ld2450_zone_t *, const uint8_t *, size_t,
                 uint64_t receipt_us, ld2450_report_t, void *);
#endif
