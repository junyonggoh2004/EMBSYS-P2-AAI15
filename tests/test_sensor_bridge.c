#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mqtt/sensor_bridge.h"
#include "bus/bus_common.h"
#include "application/sensor_measurement.h"

static unsigned published;
static uint8_t last_data[64];
static int last_length;
static unsigned ml_only_frames;
static bool source_stamp_valid;
static uint32_t source_stamp_ms;
void sensor_measurement_remote_stamp(bool valid, uint32_t source_ms) {
    source_stamp_valid = valid;
    source_stamp_ms = source_ms;
}
bool sensor_measurement_remote_ml_only(const char *name) {
    return !strcmp(name, "ML_ONLY");
}
void sensor_measurement_notify(const app_cfg_t *cfg, const uint8_t *data, int length) {
    assert(!strcmp(cfg->name, "ML_ONLY") && source_stamp_valid && source_stamp_ms == 70);
    assert(data && length == 3);
    ++ml_only_frames;
}
void publisher_emit_hex(const app_cfg_t *cfg, const char *proto,
                        const char *source, const uint8_t *bytes, int length)
{
    assert(cfg->proto == proto_mqtt && !strcmp(proto, "mqtt"));
    assert(!strcmp(source, "REMOTE_AHT"));
    assert(length > 0 && length <= (int)sizeof last_data);
    memcpy(last_data, bytes, (size_t)length);
    last_length = length;
    ++published;
}

int main(void)
{
    sensor_bridge_reset();
    assert(sensor_bridge_add_mapping("nodeB", "AHT", "REMOTE_AHT"));
    sensor_bridge_on_remote_line("nodeB", "AHT",
        "proto=i2c src=AHT len=6 ts=12 : 08 80 00 06 00 00", 100);
    assert(published == 1 && last_length == 6 && last_data[0] == 8);
    sensor_bridge_on_remote_line("nodeB", "AHT",
        "proto=i2c src=AHT len=6 ts=12 : 08 80", 101);
    assert(published == 1); /* A truncated frame must never reach rules/publication. */
    const char *bad[] = {
        "proto=i2c src=AHT len=1 ts=12 : GG",
        "proto=i2c src=AHT len=1 ts=12 : 100",
        "proto=i2c src=AHT len=1 ts=12 : 08 80",
        "proto=i2c src=OTHER len=1 ts=12 : 08",
        "proto=i2c src=AHT len=1 ts=4294967296 : 08",
        "proto=other src=AHT len=1 ts=12 : 08",
        "08 80 00 06 00 00",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        sensor_bridge_on_remote_line("nodeB", "AHT", bad[i], 102);
        assert(published == 1 && last_length == 6 && last_data[0] == 8);
    }
    sensor_bridge_on_remote_line("wrong-node", "AHT",
        "proto=i2c src=AHT len=1 ts=12 : 08", 103);
    assert(published == 1);
    sensor_bridge_on_remote_line("nodeB", "AHT",
        "proto=i2c src=AHT len=1 ts=0 : 00", 104);
    assert(published == 2 && last_length == 1 && last_data[0] == 0);
    assert(sensor_bridge_add_mapping("nodeB", "DISTANCE", "ML_ONLY"));
    sensor_bridge_on_remote_line("nodeB", "DISTANCE",
        "proto=gpio src=DISTANCE len=3 ts=70 : 00 01 01", 200);
    assert(ml_only_frames == 1 && published == 2 && !source_stamp_valid);
    sensor_bridge_reset();
    sensor_bridge_on_remote_line("nodeB", "AHT",
        "proto=i2c src=AHT len=1 ts=12 : 08", 105);
    assert(published == 2);
    puts("Sensor bridge strict-frame regression: PASS");
    return 0;
}
