#include <stdio.h>
#include "application/output_format.h"    // output_emit_hex()
#include "bus/bus_common.h"               // publisher_emit_hex() extern lives here
#include "mqtt/mqtt_telemetry.h"
#include "rule_engine/rules.h"

void output_emit_hex(const app_cfg_t *cfg,
                     const char *proto_str,
                     const char *src_str,
                     const uint8_t *buf, int n)
{
    if (!proto_str) proto_str = "";
    if (!src_str)   src_str   = "";
    if (!buf || n <= 0) return;

    uint32_t ts = app_now_ms();

    // 1) Console line (unchanged)
    printf("proto=%s src=%s len=%d ts=%lu :", proto_str, src_str, n, (unsigned long)ts);
    for (int i = 0; i < n; i++) printf(" %02X", buf[i]);
    printf("\n");

    // 2) Rule engine evaluation for this frame
    rules_on_sample(src_str /* usually cfg->name */, buf, n, ts);

    // 3) MQTT: raw bytes (binary payload)
    (void)mqtt_pub_bytes(src_str, buf, (size_t)n, /*qos*/1, /*retain*/false);

    // 4) Optional: publish the same ASCII line for debugging tools
    char line[256];
    int m = snprintf(line, sizeof line, "proto=%s src=%s len=%d ts=%lu :",
                     proto_str, src_str, n, (unsigned long)ts);
    for (int i = 0; i < n && m + 3 < (int)sizeof(line); i++) {
        m += snprintf(line + m, sizeof(line) - (size_t)m, " %02X", buf[i]);
    }
    (void)mqtt_pub_line(src_str, line, /*qos*/0, /*retain*/false);
}

// Back-compat shim for older call sites (declared extern in bus_common.h)
void publisher_emit_hex(const app_cfg_t *cfg,
                        const char *proto_str,
                        const char *src_str,
                        const uint8_t *buf, int n)
{
    output_emit_hex(cfg, proto_str, src_str, buf, n);
}
