#ifndef EDGE_SENSOR_MEASUREMENT_H
#define EDGE_SENSOR_MEASUREMENT_H
#include "bus/bus_common.h"

/* Supplementary console JSON only; raw frame/rule/MQTT contract stays intact.
 * Positive frame length = data, negative HAL_ERR_* = acquisition failure.
 * Returns 0 for generic sensors without a physical-unit decoder.
 */
/* Typed, bus-independent numeric decoding shared with console output. */
bool sensor_measurement_field_supported(const app_cfg_t *cfg,const char *field);
bool sensor_measurement_value(const app_cfg_t *cfg,const uint8_t *data,int length,const char *field,float *value);
void sensor_measurement_notify(const app_cfg_t *cfg,const uint8_t *data,int length);
int sensor_measurement_json(const app_cfg_t *cfg,const uint8_t *data,int length,
                            char *out,size_t capacity);
void sensor_measurement_emit(const app_cfg_t *cfg,const uint8_t *data,int length);
#endif
