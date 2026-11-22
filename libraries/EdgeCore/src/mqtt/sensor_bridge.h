#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "bus/bus_common.h"   // sensor_profile_t

#ifdef __cplusplus
extern "C" {
#endif

// Register mapping: remote node + remote sensor name -> local logical source name
// Example: ("nodeB", "GY511_ACC") -> "REMOTE_ACC"
bool sensor_bridge_add_mapping(const char *remote_node,
                               const char *remote_source,
                               const char *local_name);

// Called when a remote RAW frame is received (we'll mainly use the LINE version for now)
void sensor_bridge_on_remote_raw(const char *remote_node,
                                 const char *remote_source,
                                 const uint8_t *buf,
                                 size_t len,
                                 uint32_t ts_ms);

// Called when a remote LINE frame (ASCII) is received
void sensor_bridge_on_remote_line(const char *remote_node,
                                  const char *remote_source,
                                  const char *line,
                                  uint32_t ts_ms);
// Name the decoder for frames re-emitted under local_name, so typed consumers
// (console JSON, ML bindings) can read them. Called by the config owner right
// after the mapping is added; a new or updated mapping starts as raw.
// Returns false when no mapping uses local_name.
bool sensor_bridge_set_profile(const char *local_name, sensor_profile_t profile);

void sensor_bridge_reset(void);

#ifdef __cplusplus
}
#endif
