#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mqtt/sensor_frame.h"
#include "application/sensor_measurement.h"

uint32_t hal_time_ms(void) { return 100; }

static void reject_line(const char *line, size_t length)
{
    sensor_frame_t out, before;
    memset(&out, 0xa5, sizeof out);
    memcpy(&before, &out, sizeof out);
    assert(!sensor_frame_parse_line(line, length, "AHT", "i2c", &out));
    assert(!memcmp(&before, &out, sizeof out));
}

int main(void)
{
    sensor_frame_topic_t topic;
    const char *valid_topic = "pico/nodeB/sensor/AHT/line";
    assert(sensor_frame_parse_topic(valid_topic, strlen(valid_topic), &topic));
    assert(!strcmp(topic.node, "nodeB") && !strcmp(topic.source, "AHT"));
    const char *bad_topics[] = {
        "pico//sensor/AHT/line", "pico/nodeB/sensor//line",
        "pico/nodeB/sensor/AHT/line/extra", "pico/nodeB/sensor/AHT/raw",
        "pico/nodeB/sensor/AHT/lineX", "prefix/pico/nodeB/sensor/AHT/line",
        "pico/+/sensor/AHT/line", "pico/nodeB/sensor/#/line",
        "pico/nodeB/sensor/A HT/line",
        "pico/12345678901234567890123456789012/sensor/AHT/line",
        "pico/nodeB/sensor/12345678901234567890123456789012/line",
    };
    for (size_t i = 0; i < sizeof bad_topics / sizeof bad_topics[0]; ++i) {
        sensor_frame_topic_t before = topic;
        assert(!sensor_frame_parse_topic(bad_topics[i], strlen(bad_topics[i]), &topic));
        assert(!memcmp(&before, &topic, sizeof topic));
    }
    char topic_nul[] = "pico/nodeB/sensor/AHT/line\0junk";
    assert(!sensor_frame_parse_topic(topic_nul, sizeof topic_nul - 1, &topic));
    assert(!sensor_frame_parse_topic(NULL, 0, &topic));
    assert(!sensor_frame_parse_topic(valid_topic, strlen(valid_topic), NULL));

    const char *valid = "proto=i2c src=AHT len=6 ts=4294967295 : 08 80 00 06 00 00";
    sensor_frame_t frame;
    assert(sensor_frame_parse_line(valid, strlen(valid), "AHT", "i2c", &frame));
    assert(frame.length == 6 && frame.timestamp_ms == UINT32_MAX);
    assert(!strcmp(frame.protocol, "i2c") && !strcmp(frame.source, "AHT"));
    app_cfg_t cfg = {.proto = proto_i2c, .i2c_sensor = I2C_SENSOR_AHT};
    float value;
    assert(sensor_measurement_value(&cfg, frame.data, (int)frame.length, "temperature_c", &value));
    assert(value == 25.0f);
    assert(sensor_measurement_value(&cfg, frame.data, (int)frame.length, "humidity_pct", &value));
    assert(value == 50.0f);
    /* A raw source name must not select a decoder without a configured profile. */
    cfg.i2c_sensor = I2C_SENSOR_RAW;
    assert(!sensor_measurement_value(&cfg, frame.data, (int)frame.length, "temperature_c", &value));

    const char *lower = "proto=spi\tsrc=AHT\tlen=2\tts=0 : ab Cd\r\n";
    assert(sensor_frame_parse_line(lower, strlen(lower), "AHT", NULL, &frame));
    assert(frame.data[0] == 0xab && frame.data[1] == 0xcd);
    assert(!sensor_frame_parse_line(valid, strlen(valid), "OTHER", NULL, &frame));
    assert(!sensor_frame_parse_line(valid, strlen(valid), "AHT", "gpio", &frame));
    assert(!sensor_frame_parse_line(valid, strlen(valid), NULL, NULL, &frame));
    assert(!sensor_frame_parse_line(valid, strlen(valid), "AHT", NULL, NULL));
    assert(!sensor_frame_parse_line(NULL, 0, "AHT", NULL, &frame));

    const char *bad_lines[] = {
        "", "08 80 00 06 00 00", "proto=i2c src=AHT len=1 ts=0 00",
        "proto=unknown src=AHT len=1 ts=0 : 00",
        "proto=i2c src=OTHER len=1 ts=0 : 00",
        "proto=i2c src=AHT len=0 ts=0 :",
        "proto=i2c src=AHT len=65 ts=0 : 00",
        "proto=i2c src=AHT len=-1 ts=0 : 00",
        "proto=i2c src=AHT len=+1 ts=0 : 00",
        "proto=i2c src=AHT len=4294967297 ts=0 : 00",
        "proto=i2c src=AHT len=1 ts=-1 : 00",
        "proto=i2c src=AHT len=1 ts=4294967296 : 00",
        "proto=i2c src=AHT len=1 ts=0x0 : 00",
        "proto=i2c src=AHT len=2 ts=0 : 00",
        "proto=i2c src=AHT len=1 ts=0 : 00 FF",
        "proto=i2c src=AHT len=1 ts=0 : GG",
        "proto=i2c src=AHT len=1 ts=0 : 0G",
        "proto=i2c src=AHT len=1 ts=0 : 100",
        "proto=i2c src=AHT len=1 ts=0 : 0x00",
        "proto=i2c src=AHT len=1 ts=0 : -1",
        "proto=i2c src=AHT len=1 ts=0 : F",
        "proto=i2c src=AHT len=2 ts=0 : 00,FF",
        "proto=i2c src=AHT len=2 ts=0 : 00FF",
        "proto=i2c src=AHT len=1 ts=0 : 00junk",
        "proto=i2c\nsrc=AHT len=1 ts=0 : 00",
        "proto=i2c src=AHT len=1 ts=0 : 00 proto=i2c",
        "proto=i2c src=AHT len=1 ts=0 len=1 : 00",
    };
    for (size_t i = 0; i < sizeof bad_lines / sizeof bad_lines[0]; ++i)
        reject_line(bad_lines[i], strlen(bad_lines[i]));
    char nul[] = "proto=i2c src=AHT len=1 ts=0 : 00\0 FF";
    reject_line(nul, sizeof nul - 1);
    /* Byte spans need no terminator. Exercise every truncation of a real line. */
    for (size_t n = 0; n < strlen(valid); ++n) {
        char span[128];
        memcpy(span, valid, n);
        reject_line(span, n);
    }
    char full[256];
    size_t used = (size_t)snprintf(full, sizeof full, "proto=i2c src=AHT len=64 ts=0 :");
    for (size_t i = 0; i < 64; ++i) used += (size_t)snprintf(full + used, sizeof full - used, " %02X", (unsigned)i);
    assert(sensor_frame_parse_line(full, used, "AHT", "i2c", &frame));
    assert(frame.length == 64 && frame.data[63] == 63);
    memset(full + used, ' ', sizeof full - used);
    assert(sensor_frame_parse_line(full, 255, "AHT", "i2c", &frame));
    reject_line(full, 256);
    puts("Sensor frame parser: PASS (bounds, rejection atomicity, topic identity and typed AHT decoding)");
    return 0;
}
