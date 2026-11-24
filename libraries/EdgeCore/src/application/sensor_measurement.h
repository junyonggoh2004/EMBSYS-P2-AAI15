#ifndef EDGE_SENSOR_MEASUREMENT_H
#define EDGE_SENSOR_MEASUREMENT_H
#include "bus/bus_common.h"

/* Supplementary console JSON only; raw frame/rule/MQTT contract stays intact.
 * Positive frame length = data, negative HAL_ERR_* = acquisition failure.
 * Returns 0 for generic sensors without a physical-unit decoder.
 */
/* Typed, bus-independent numeric decoding shared with console output.
 * proto=mqtt sensors decode by their remote_profile (raw: no fields). */
sensor_profile_t sensor_measurement_profile(const app_cfg_t *cfg);
bool sensor_measurement_field_supported(const app_cfg_t *cfg,const char *field);
bool sensor_measurement_value(const app_cfg_t *cfg,const uint8_t *data,int length,const char *field,float *value);
void sensor_measurement_notify(const app_cfg_t *cfg,const uint8_t *data,int length);
/* Valid only during emission of one bridged MQTT frame. Source uptime is used
   for sample spacing; consumers still use local time for freshness. */
void sensor_measurement_remote_stamp(bool valid, uint32_t source_ms);
/* An isolated high-rate ML bridge can consume the frame without re-publishing
   its raw and text aliases; ordinary bridge telemetry is unchanged. */
bool sensor_measurement_remote_ml_only(const char *local_name);
int sensor_measurement_json(const app_cfg_t *cfg,const uint8_t *data,int length,
                            char *out,size_t capacity);
void sensor_measurement_emit(const app_cfg_t *cfg,const uint8_t *data,int length);
#endif
