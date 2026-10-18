// runtime_s3.h
// FreeRTOS runtime for the ESP32-S3 (D1 task 13), following the Week 6 design
// review: SchedulerTask owns all configuration and sensor state, and the other
// tasks reach it only through queues.
#ifndef RUNTIME_S3_H
#define RUNTIME_S3_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------- sched_q: everything that changes configuration or state ----------

#define SCHED_TOPIC_MAX 96
#define SCHED_TEXT_MAX  2048   // matches the MQTT payload limit

typedef enum {
    SCHED_CONSOLE_LINE = 1,    // a complete REPL line from ControlTask
    SCHED_MQTT_CONFIG,         // payload from pico/<node>/config or broadcast/config
    SCHED_MQTT_CMD,            // payload from pico/<node>/cmd
    SCHED_MQTT_NODE,           // payload from pico/<node>/in
    SCHED_REMOTE_LINE,         // a followed node's sensor line; topic says which
    SCHED_MODEL_PARAMS,        // pico/<node>/model/params (binary; len is set)
} sched_kind_t;

typedef struct {
    uint8_t kind;              // sched_kind_t
    uint16_t len;              // payload length, for binary payloads
    char    topic[SCHED_TOPIC_MAX];
    char    text[SCHED_TEXT_MAX];
} sched_msg_t;

// ---------- pub_q: everything MQTTTask publishes ----------

#define PUB_TOPIC_MAX   160
#define PUB_PAYLOAD_MAX 512

typedef enum {
    PUB_PUBLISH = 0,
    PUB_SUBSCRIBE,             // topic only; MQTTTask owns the client, so others ask
} pub_op_t;

typedef struct {
    uint8_t  op;               // pub_op_t
    char     topic[PUB_TOPIC_MAX];
    char     payload[PUB_PAYLOAD_MAX];
    uint16_t len;
    uint8_t  qos;
    bool     retain;
} pub_msg_t;

// ---------- bench_q: timing records printed by BenchmarkTask ----------

typedef struct {
    char     name[24];
    uint32_t value_us;
} bench_msg_t;

// net_events bits, set by MQTTTask
#define NET_WIFI_UP (1u << 0)
#define NET_MQTT_UP (1u << 1)

extern QueueHandle_t sched_q;
extern QueueHandle_t pub_q;
extern QueueHandle_t infer_q;
extern QueueHandle_t app_q;
extern QueueHandle_t bench_q;
extern EventGroupHandle_t net_events;

// Wi-Fi and broker settings for MQTTTask (task 19). Call before
// runtime_start(); without it the node runs offline.
void runtime_set_network(const char *ssid, const char *pass, const char *mqtt_host,
                         uint16_t mqtt_port, const char *node_id);

// Creates the queues and tasks. Call once from setup(), after
// platform_console_begin().
void runtime_start(void);

// MonitorTask body, run from the Arduino loop(): checks stacks and heap once a
// second and reports any task close to overflowing.
void runtime_monitor_tick(void);

// Prints every task's stack headroom and the heap (console command TASKS).
void runtime_print_tasks(void);

// Records a timing sample for BenchmarkTask without blocking the caller.
void runtime_bench(const char *name, uint32_t value_us);

// Called by the S3 MQTT layer (task 19) to register the core's MQTT
// configuration, command and node-message handlers. SchedulerTask runs them.
void runtime_set_mqtt_handlers(void (*cfg)(const char *), void (*cmd)(const char *),
                               void (*node)(const char *, const char *));

#ifdef __cplusplus
}
#endif

#endif // RUNTIME_S3_H
