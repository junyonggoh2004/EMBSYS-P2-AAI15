#ifndef EDGE_SENSOR_FRAME_H
#define EDGE_SENSOR_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Match the existing bridge name/data capacities and publisher's line buffer.
 * These are transport bounds, not a sensor profile or freshness policy. */
#define SENSOR_FRAME_NAME_CAP 32u
#define SENSOR_FRAME_DATA_CAP 64u
#define SENSOR_FRAME_LINE_MAX 255u

typedef struct {
    char node[SENSOR_FRAME_NAME_CAP];
    char source[SENSOR_FRAME_NAME_CAP];
} sensor_frame_topic_t;

typedef struct {
    char protocol[5]; /* i2c, uart, gpio, mqtt, spi */
    char source[SENSOR_FRAME_NAME_CAP];
    uint32_t timestamp_ms; /* source uptime; not comparable across boards */
    size_t length;
    uint8_t data[SENSOR_FRAME_DATA_CAP];
} sensor_frame_t;

/* Inputs are byte spans: no terminator is required or included in length.
 * Reject embedded NUL, wildcard/empty/overlong names and extra topic segments.
 * On failure outputs remain unchanged. No allocation or global state. */
bool sensor_frame_parse_topic(const char *topic, size_t length,
                              sensor_frame_topic_t *out);

/* Parse publisher format, with exact two-digit hex bytes and a nonzero length
 * equal to the byte count. Decimal fields must fit uint32_t. Only horizontal
 * whitespace separates fields; trailing whitespace/CRLF is allowed.
 * expected_source is required. expected_protocol may be NULL for the generic
 * bridge; typed consumers must pass their configured protocol/profile.
 * Rejection must not be used to refresh a consumer's last-valid receive age. */
bool sensor_frame_parse_line(const char *line, size_t length,
                             const char *expected_source,
                             const char *expected_protocol,
                             sensor_frame_t *out);

#ifdef __cplusplus
}
#endif
#endif
