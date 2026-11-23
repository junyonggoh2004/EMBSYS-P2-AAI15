#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mqtt/sensor_bridge.h"
#include "bus/bus_common.h"

static unsigned published;
static uint8_t last_data[64];
static int last_length;
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
    sensor_bridge_reset();
    sensor_bridge_on_remote_line("nodeB", "AHT",
        "proto=i2c src=AHT len=1 ts=12 : 08", 105);
    assert(published == 2);
    puts("Sensor bridge strict-frame regression: PASS");
    return 0;
}
