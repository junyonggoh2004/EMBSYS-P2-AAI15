#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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

// Called with a complete publisher LINE envelope (proto/src/len/ts + hex).
// Bare hex, bad source/length/hex and oversized frames are rejected before
// rule/publication side effects. Callers receiving byte spans must reject
// embedded NULs before converting to this C-string API (MTK does so).
void sensor_bridge_on_remote_line(const char *remote_node,
                                  const char *remote_source,
                                  const char *line,
                                  uint32_t ts_ms);
void sensor_bridge_reset(void);

#ifdef __cplusplus
}
#endif
