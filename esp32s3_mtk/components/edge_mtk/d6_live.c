/* Board B's live environmental snapshot. The bridge has already checked the
 * remote node and decoded profile before these measurements reach us. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal/hal.h"
#include "application/sensor_measurement.h"
#include "mqtt/mqtt_telemetry.h"
#include "runtime_mtk.h"

#define D6_SAMPLE_AGE_MS 6000u
#define D6_PUBLISH_PERIOD_MS 2000u

typedef struct {
    float value;
    uint32_t at;
    bool valid;
} reading_t;

static reading_t temperature, humidity, light, sonar_a, sonar_b;
static uint32_t last_publish, sequence;

static bool fresh(const reading_t *reading, uint32_t now)
{
    return reading->valid && now - reading->at < D6_SAMPLE_AGE_MS;
}

static void update(reading_t *reading, const app_cfg_t *cfg, const uint8_t *data,
                   int length, const char *field, uint32_t now)
{
    float value;
    reading->valid = sensor_measurement_value(cfg, data, length, field, &value);
    if (reading->valid) {
        reading->value = value;
        reading->at = now;
    }
}

void d6_live_sensor_notify(const app_cfg_t *cfg, const uint8_t *data, int length)
{
    if (!cfg || strcmp(runtime_node_id(), "s3-002") != 0) return;
    uint32_t now = hal_time_ms();
    if (!strcmp(cfg->name, "A_AHT")) {
        update(&temperature, cfg, data, length, "temperature_c", now);
        update(&humidity, cfg, data, length, "humidity_pct", now);
    } else if (!strcmp(cfg->name, "A_LIGHT")) {
        update(&light, cfg, data, length, "light_lux", now);
    } else if (!strcmp(cfg->name, "A_SONAR_A")) {
        update(&sonar_a, cfg, data, length, "distance_cm", now);
    } else if (!strcmp(cfg->name, "A_SONAR_B")) {
        update(&sonar_b, cfg, data, length, "distance_cm", now);
    } else return;

    if (last_publish && now - last_publish < D6_PUBLISH_PERIOD_MS) return;
    if (!fresh(&temperature, now) || !fresh(&humidity, now) || !fresh(&light, now)) return;
    float a = fresh(&sonar_a, now) ? sonar_a.value : -1.0f;
    float b = fresh(&sonar_b, now) ? sonar_b.value : -1.0f;
    unsigned lux = light.value >= 0.0f && light.value <= 65535.0f ?
                   (unsigned)light.value : 65536u;
    if (lux > 65535u) return;
    uint32_t next = sequence + 1u;
    if (next == 0) next = 1;
    /* Occupancy is explicitly pending; zero is a schema placeholder, not a count. */
    if (mqtt_pub_text("d6/board-b/snapshot",
        "{\"v\":3,\"seq\":%lu,\"occupancy\":0,\"distance_a_cm\":%.1f,"
        "\"distance_b_cm\":%.1f,\"temperature_c\":%.2f,\"humidity_pct\":%.2f,"
        "\"light_lux\":%u,\"source\":\"live\"}",
        (unsigned long)next, (double)a, (double)b, (double)temperature.value,
        (double)humidity.value, lux)) {
        sequence = next;
        last_publish = now;
    }
}
