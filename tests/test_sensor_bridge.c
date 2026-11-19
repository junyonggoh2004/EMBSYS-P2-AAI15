#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mqtt/sensor_bridge.h"
#include "bus/bus_common.h"
#include "application/sensor_measurement.h"

static unsigned published;
static uint8_t last_data[64];
static int last_length;
static app_cfg_t last_cfg;
uint32_t hal_time_ms(void) { return 0; }   // sensor_measurement console JSON only
void publisher_emit_hex(const app_cfg_t *cfg, const char *proto,
                        const char *source, const uint8_t *bytes, int length)
{
    assert(cfg->proto == proto_mqtt && !strcmp(proto, "mqtt"));
    assert(!strcmp(source, "REMOTE_AHT"));
    assert(length > 0 && length <= (int)sizeof last_data);
    memcpy(last_data, bytes, (size_t)length);
    last_cfg = *cfg;
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
    /* A typed mapping hands the decoder to consumers; a re-added mapping is raw again. */
    sensor_bridge_reset();
    assert(!sensor_bridge_set_profile("REMOTE_AHT", SENSOR_PROFILE_AHT)); /* no mapping yet */
    assert(sensor_bridge_add_mapping("nodeB", "AHT", "REMOTE_AHT"));
    sensor_bridge_on_remote_line("nodeB", "AHT",
        "proto=i2c src=AHT len=6 ts=12 : 08 80 00 06 00 00", 105);
    float value;
    assert(last_cfg.remote_profile == SENSOR_PROFILE_RAW);
    assert(!sensor_measurement_value(&last_cfg, last_data, last_length, "temperature_c", &value));
    assert(sensor_bridge_set_profile("REMOTE_AHT", SENSOR_PROFILE_AHT));
    sensor_bridge_on_remote_line("nodeB", "AHT",
        "proto=i2c src=AHT len=6 ts=13 : 08 80 00 06 00 00", 106);
    assert(last_cfg.remote_profile == SENSOR_PROFILE_AHT);
    assert(sensor_measurement_value(&last_cfg, last_data, last_length, "temperature_c", &value) && value == 25.0f);
    assert(sensor_measurement_value(&last_cfg, last_data, last_length, "humidity_pct", &value) && value == 50.0f);
    assert(sensor_bridge_add_mapping("nodeB", "AHT", "REMOTE_AHT"));
    sensor_bridge_on_remote_line("nodeB", "AHT",
        "proto=i2c src=AHT len=6 ts=14 : 08 80 00 06 00 00", 107);
    assert(last_cfg.remote_profile == SENSOR_PROFILE_RAW);
    sensor_bridge_reset();
    sensor_bridge_on_remote_line("nodeB", "AHT",
        "proto=i2c src=AHT len=1 ts=12 : 08", 108);
    assert(published == 5);   /* two earlier + three typed-profile frames; none after reset */
    puts("Sensor bridge strict-frame regression: PASS");
    return 0;
}
