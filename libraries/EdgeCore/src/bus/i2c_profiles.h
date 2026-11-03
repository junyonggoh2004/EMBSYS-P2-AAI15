#ifndef EDGE_I2C_PROFILES_H
#define EDGE_I2C_PROFILES_H
#include "bus/bus_common.h"

typedef struct i2c_profile i2c_profile_t;
int i2c_profile_create(const app_cfg_t *cfg, int bus, i2c_profile_t **out);
int i2c_profile_poll(i2c_profile_t *profile, uint8_t *data, size_t capacity);
void i2c_profile_cancel(i2c_profile_t *profile);
void i2c_profile_free(i2c_profile_t *profile);

/* Raw frame: AHT = native 6 bytes (no CRC); TSL = ch0/ch1 LE16 + lux LE32.
 * No valid frame is returned on a bus error, invalid ID/status or saturation.
 */
float i2c_tsl_lux(uint16_t ch0, uint16_t ch1, bool gain16, uint8_t part);
#endif
