#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "application/sensor_measurement.h"

void d6_live_sensor_notify(const app_cfg_t *, const uint8_t *, int);

static uint32_t now = 1000;
static unsigned publishes;
static char payload[512];

uint32_t hal_time_ms(void) { return now; }
const char *runtime_node_id(void) { return "s3-002"; }
bool mqtt_pub_text(const char *topic, const char *fmt, ...)
{
    assert(!strcmp(topic, "d6/board-b/snapshot"));
    va_list args;
    va_start(args, fmt);
    vsnprintf(payload, sizeof payload, fmt, args);
    va_end(args);
    publishes++;
    return true;
}

int main(void)
{
    app_cfg_t aht = { .proto = proto_mqtt, .remote_profile = SENSOR_PROFILE_AHT };
    app_cfg_t light = { .proto = proto_mqtt, .remote_profile = SENSOR_PROFILE_TSL2561 };
    app_cfg_t sonar = { .proto = proto_mqtt, .remote_profile = SENSOR_PROFILE_PULSE };
    snprintf(aht.name, sizeof aht.name, "A_AHT");
    snprintf(light.name, sizeof light.name, "A_LIGHT");
    snprintf(sonar.name, sizeof sonar.name, "A_SONAR_A");
    const uint8_t aht_frame[] = {0x18, 0xb4, 0x5a, 0xc5, 0xbe, 0x7a};
    const uint8_t light_frame[] = {0, 0, 0, 0, 0xa4, 0x01, 0, 0};
    const uint8_t sonar_frame[] = {0xd0, 0x07, 1};

    d6_live_sensor_notify(&aht, aht_frame, sizeof aht_frame);
    assert(publishes == 0);
    d6_live_sensor_notify(&light, light_frame, sizeof light_frame);
    assert(publishes == 1 && strstr(payload, "\"v\":3,\"seq\":1"));
    assert(strstr(payload, "\"distance_a_cm\":-1.0"));
    assert(strstr(payload, "\"light_lux\":420"));

    now += 2100;
    d6_live_sensor_notify(&sonar, sonar_frame, sizeof sonar_frame);
    assert(publishes == 2 && strstr(payload, "\"seq\":2"));
    assert(strstr(payload, "\"distance_a_cm\":34.3"));

    now += 6100;
    d6_live_sensor_notify(&aht, aht_frame, sizeof aht_frame);
    assert(publishes == 2); /* stale light may not refresh old values */
    d6_live_sensor_notify(&light, light_frame, sizeof light_frame);
    assert(publishes == 3 && strstr(payload, "\"seq\":3"));
    assert(strstr(payload, "\"distance_a_cm\":-1.0"));
    puts("Board B live D6 snapshot: PASS");
    return 0;
}
