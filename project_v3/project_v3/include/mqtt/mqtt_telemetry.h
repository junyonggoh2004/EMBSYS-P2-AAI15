#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callback when a full config block (BEGINCFG..ENDCFG) is received via MQTT
typedef void (*mqtt_cfg_block_cb_t)(const char *block_text);

// Callback for one-line commands (e.g. RUN/STOP/CLEAR etc.)
typedef void (*mqtt_cmd_cb_t)(const char *cmd_line);

// Callback for generic node-to-node messages on pico/<node>/in
typedef void (*mqtt_node_msg_cb_t)(const char *from_node, const char *payload);

// Callback for remote sensor frames (after mapping to local alias)
typedef void (*mqtt_remote_sensor_cb_t)(
    const char *alias_name,         // local sensor name / alias
    const uint8_t *bytes,           // raw bytes as published
    size_t len,
    uint32_t ts_ms                  // timestamp (ms since boot on THIS node)
);

// Basic init / lifecycle
bool mqtt_init(const char *broker_host,
               uint16_t broker_port,
               const char *client_id,
               mqtt_cfg_block_cb_t cfg_cb,
               mqtt_cmd_cb_t       cmd_cb,
               mqtt_node_msg_cb_t  node_cb);

void mqtt_poll(void);   // must be called regularly from main loop
bool mqtt_is_connected(void);//helper to check connection status

// Node identity and topics
void mqtt_set_node_id(const char *node_id);

// Simple text publishing
bool mqtt_pub_text(const char *topic, const char *fmt, ...);

// JSON telemetry (base64 encoded sensor bytes)
bool mqtt_pub_json_sensor(const char *sensor_name,
                          const char *proto,
                          const uint8_t *bytes,
                          size_t len,
                          uint32_t seq,
                          uint32_t ts_ms);

// Binary/raw payload under pico/<node>/sensor/<name>/raw
bool mqtt_pub_bytes(const char *sensor_name,
                    const uint8_t *bytes,
                    size_t len,
                    int qos,
                    bool retain);

// Human-readable ASCII line under pico/<node>/sensor/<name>/line
bool mqtt_pub_line(const char *sensor_name,
                   const char *line,
                   int qos,
                   bool retain);

// Presence / heartbeat
void mqtt_publish_birth(bool retained);
void mqtt_publish_heartbeat(uint32_t uptime_s);

// Direct node-to-node messages via pico/<to>/in
bool mqtt_send_to_node(const char *to, const char *fmt, ...);

// Inter-node follow/unfollow (wildcard style)
bool mqtt_follow_node_all(const char *other_node_id, int qos);
bool mqtt_follow_node_raw(const char *other_node_id, int qos);
bool mqtt_follow_node_line(const char *other_node_id, int qos);

bool mqtt_unfollow_node_all(const char *other_node_id);
bool mqtt_unfollow_node_raw(const char *other_node_id);
bool mqtt_unfollow_node_line(const char *other_node_id);

// Fine-grained follow for a specific remote sensor as a “local alias”
bool mqtt_follow_remote_source(const char *other_node_id,
                               const char *remote_source,
                               const char *local_alias);

// Register callback for remote sensor frames (after mapping)
void mqtt_set_remote_sensor_cb(mqtt_remote_sensor_cb_t cb);

// Rule event helper (you already use this from rule_action.c)
bool mqtt_pub_event(const char *rule_name,
                    const char *event_type,
                    double value,
                    const char *msg,
                    uint32_t ts_ms);
bool mqtt_pub_cfg_status(const char *name, bool ok);
bool mqtt_pub_rule_status(const char *name, bool ok);

#ifdef __cplusplus
}
#endif
