#pragma once
#include <stdint.h>
#include "bus/bus_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// New canonical emitter
void output_emit_hex(const app_cfg_t *cfg,
                     const char *proto_str,
                     const char *src_str,
                     const uint8_t *buf, int n);

#ifdef __cplusplus
}
#endif
