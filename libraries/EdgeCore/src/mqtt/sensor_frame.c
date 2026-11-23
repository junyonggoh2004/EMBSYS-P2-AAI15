#include "mqtt/sensor_frame.h"
#include <string.h>

typedef struct { const char *p, *end; } cursor_t;

static bool name_valid(const char *p, size_t n)
{
    if (!n || n >= SENSOR_FRAME_NAME_CAP) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char ch = (unsigned char)p[i];
        if (ch <= 0x20u || ch >= 0x7fu || ch == '/' || ch == '+' || ch == '#')
            return false;
    }
    return true;
}

bool sensor_frame_parse_topic(const char *topic, size_t length,
                              sensor_frame_topic_t *out)
{
    /* pico/ + two names + /sensor/ + /line */
    if (!topic || !out || length < 20u || length > 80u ||
        memcmp(topic, "pico/", 5) || memchr(topic, '\0', length)) return false;
    const char *end = topic + length;
    const char *node = topic + 5;
    const char *sep = memchr(node, '/', (size_t)(end - node));
    if (!sep || !name_valid(node, (size_t)(sep - node)) ||
        end - sep < 8 || memcmp(sep, "/sensor/", 8)) return false;
    size_t node_length = (size_t)(sep - node);
    const char *source = sep + 8;
    sep = memchr(source, '/', (size_t)(end - source));
    if (!sep || !name_valid(source, (size_t)(sep - source)) ||
        end - sep != 5 || memcmp(sep, "/line", 5)) return false;
    sensor_frame_topic_t result = {0};
    memcpy(result.node, node, node_length);
    memcpy(result.source, source, (size_t)(sep - source));
    *out = result;
    return true;
}

static bool literal(cursor_t *c, const char *text)
{
    size_t n = strlen(text);
    if ((size_t)(c->end - c->p) < n || memcmp(c->p, text, n)) return false;
    c->p += n;
    return true;
}

static bool hspace(cursor_t *c)
{
    const char *start = c->p;
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t')) ++c->p;
    return c->p != start;
}

static bool token(cursor_t *c, char *out, size_t capacity)
{
    const char *start = c->p;
    while (c->p < c->end && *c->p != ' ' && *c->p != '\t') ++c->p;
    size_t n = (size_t)(c->p - start);
    if (n >= capacity || !name_valid(start, n)) return false;
    memcpy(out, start, n);
    out[n] = 0;
    return true;
}

static bool decimal(cursor_t *c, uint32_t *out)
{
    const char *start = c->p;
    uint32_t value = 0;
    while (c->p < c->end && *c->p >= '0' && *c->p <= '9') {
        uint32_t digit = (uint32_t)(*c->p++ - '0');
        if (value > (UINT32_MAX - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    *out = value;
    return c->p != start;
}

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool sensor_frame_parse_line(const char *line, size_t length,
                             const char *expected_source,
                             const char *expected_protocol,
                             sensor_frame_t *out)
{
    if (!line || !out || !expected_source || !length ||
        length > SENSOR_FRAME_LINE_MAX || memchr(line, '\0', length)) return false;
    cursor_t c = {line, line + length};
    sensor_frame_t result = {0};
    uint32_t count;
    if (!literal(&c, "proto=") || !token(&c, result.protocol, sizeof result.protocol) ||
        !hspace(&c) || !literal(&c, "src=") ||
        !token(&c, result.source, sizeof result.source) ||
        strcmp(result.source, expected_source) ||
        !hspace(&c) || !literal(&c, "len=") || !decimal(&c, &count) ||
        !count || count > SENSOR_FRAME_DATA_CAP ||
        !hspace(&c) || !literal(&c, "ts=") || !decimal(&c, &result.timestamp_ms) ||
        !hspace(&c) || !literal(&c, ":")) return false;
    if (strcmp(result.protocol, "i2c") && strcmp(result.protocol, "uart") &&
        strcmp(result.protocol, "gpio") && strcmp(result.protocol, "mqtt") &&
        strcmp(result.protocol, "spi")) return false;
    if (expected_protocol && strcmp(result.protocol, expected_protocol)) return false;
    result.length = count;
    for (size_t i = 0; i < result.length; ++i) {
        if (!hspace(&c) || c.end - c.p < 2) return false;
        int hi = hex_digit(c.p[0]), lo = hex_digit(c.p[1]);
        if (hi < 0 || lo < 0) return false;
        result.data[i] = (uint8_t)((hi << 4) | lo);
        c.p += 2;
    }
    while (c.p < c.end && (*c.p == ' ' || *c.p == '\t' || *c.p == '\r' || *c.p == '\n')) ++c.p;
    if (c.p != c.end) return false;
    *out = result;
    return true;
}
