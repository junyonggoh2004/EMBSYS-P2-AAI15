#include "application/ld2450.h"
#include <string.h>
static int coordinate(const uint8_t *p)
{
    unsigned n = p[0] | (unsigned)p[1] << 8;
    return n & 0x8000u ? (int)(n & 0x7fffu) : -(int)n;
}
void ld2450_feed(ld2450_decoder_t *d, const ld2450_zone_t *zone, const uint8_t *data,
                 size_t length, uint64_t now, ld2450_report_t report, void *context)
{
    static const uint8_t header[] = {0xaa, 0xff, 3, 0};
    if (!d || !zone || !data || !report) return;
    for (size_t i = 0; i < length; ++i) {
        d->bytes[d->used++] = data[i];
        while (d->used && memcmp(d->bytes, header, d->used < 4 ? d->used : 4)) {
            memmove(d->bytes, d->bytes + 1, --d->used);
        }
        if (d->used != sizeof d->bytes) continue;
        if (d->bytes[28] != 0x55 || d->bytes[29] != 0xcc) {
            ++d->malformed;
            memmove(d->bytes, d->bytes + 1, --d->used);
            continue;
        }
        unsigned count = 0;
        for (unsigned n = 0; n < 3; ++n) {
            const uint8_t *p = d->bytes + 4 + 8 * n;
            unsigned resolution = p[6] | (unsigned)p[7] << 8;
            int x = coordinate(p), y = coordinate(p + 2);
            if (resolution && x >= zone->xmin && x <= zone->xmax && y >= zone->ymin && y <= zone->ymax)
                ++count;
        }
        ++d->frames;
        d->used = 0;
        report(count, now, context);
    }
}
