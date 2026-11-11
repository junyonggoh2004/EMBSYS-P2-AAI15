// mqtt/sensor_bridge.c
#include "mqtt/sensor_bridge.h"
#include "mqtt/sensor_frame.h"
#include "application/output_format.h"  // publisher_emit_hex
#include "bus/bus_common.h"            // app_cfg_t, proto_t, sample_mode_*
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_BRIDGE_MAP 8

typedef struct {
    bool used;
    char remote_node[32];
    char remote_source[32];
    char local_name[16];   // matches app_cfg_t.name size
} bridge_map_t;

static bridge_map_t s_maps[MAX_BRIDGE_MAP];

/* ----------------- helpers ----------------- */

static void trim_copy(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || !dst_sz) return;
    if (!src) { dst[0] = 0; return; }

    // skip leading spaces
    while (*src == ' ' || *src == '\t') src++;
    size_t n = strlen(src);
    while (n && (src[n-1] == ' ' || src[n-1] == '\t' ||
                 src[n-1] == '\r' || src[n-1] == '\n')) {
        n--;
    }
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static bridge_map_t* find_map(const char *remote_node,
                              const char *remote_source)
{
    for (int i = 0; i < MAX_BRIDGE_MAP; i++) {
        if (!s_maps[i].used) continue;
        if (!strcmp(s_maps[i].remote_node,  remote_node) &&
            !strcmp(s_maps[i].remote_source, remote_source)) {
            return &s_maps[i];
        }
    }
    return NULL;
}

/* ----------------- public API ----------------- */

bool sensor_bridge_add_mapping(const char *remote_node,
                               const char *remote_source,
                               const char *local_name)
{
    if (!remote_node || !*remote_node ||
        !remote_source || !*remote_source ||
        !local_name || !*local_name) {
        printf("[BRIDGE] add mapping: invalid args\n");
        return false;
    }

    // update if exists
    bridge_map_t *m = find_map(remote_node, remote_source);
    if (!m) {
        for (int i = 0; i < MAX_BRIDGE_MAP; i++) {
            if (!s_maps[i].used) {
                m = &s_maps[i];
                m->used = true;
                break;
            }
        }
    }
    if (!m) {
        printf("[BRIDGE] add mapping: table full\n");
        return false;
    }

    trim_copy(m->remote_node,   sizeof m->remote_node,   remote_node);
    trim_copy(m->remote_source, sizeof m->remote_source, remote_source);
    trim_copy(m->local_name,    sizeof m->local_name,    local_name);

    printf("[BRIDGE] updated mapping: %s/%s -> %s\n",
           m->remote_node, m->remote_source, m->local_name);
    return true;
}

/*
 * Called from mqtt_telemetry.c when we receive:
 *   pico/<remote_node>/sensor/<remote_source>/line
 * with the complete publisher envelope (proto/src/len/ts and hex bytes).
 */
void sensor_bridge_on_remote_line(const char *remote_node,
                                  const char *remote_source,
                                  const char *line,
                                  uint32_t ts_ms)
{
    if (!remote_node || !remote_source || !line) return;

    bridge_map_t *m = find_map(remote_node, remote_source);
    if (!m) {
        printf("[BRIDGE] on_remote_line: no mapping for %s/%s (line='%s')\n",
               remote_node, remote_source, line);
        return;
    }

    (void)ts_ms; /* Receive-age policy belongs to a typed consumer. */
    sensor_frame_t frame;
    if (!sensor_frame_parse_line(line, strlen(line), remote_source, NULL, &frame)) {
        printf("[BRIDGE] rejected malformed frame from %s/%s\n", remote_node, remote_source);
        return;
    }

    // Fake a config block to re-use publisher_emit_hex() path
    app_cfg_t fake_cfg;
    memset(&fake_cfg, 0, sizeof(fake_cfg));
    strncpy(fake_cfg.name, m->local_name, sizeof(fake_cfg.name) - 1);
    fake_cfg.name[sizeof(fake_cfg.name) - 1] = 0;
    fake_cfg.proto   = proto_mqtt;
    fake_cfg.mode    = sample_mode_stream;
    fake_cfg.freq_hz = 1;

    // This will:
    //  - print hex line
    //  - feed bytes into rule engine as if it came from a local sensor
    publisher_emit_hex(&fake_cfg, "mqtt", m->local_name, frame.data, (int)frame.length);
}


void sensor_bridge_reset(void) {
    for (int i = 0; i < MAX_BRIDGE_MAP; i++) {
        s_maps[i].used = false;
        s_maps[i].remote_node[0] = 0;
        s_maps[i].remote_source[0] = 0;
        s_maps[i].local_name[0] = 0;
    }
    printf("[BRIDGE] reset\n");
}
