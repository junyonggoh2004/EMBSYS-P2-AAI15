// runtime_mtk.h
// µT-Kernel 3 SMP runtime for the ESP32-S3 (D1 task 28). Same task set and
// message formats as the FreeRTOS runtime it replaces (libraries/EdgeS3/src/
// runtime_s3.h): SchedulerTask owns all configuration and sensor state, and
// the other tasks reach it only through message buffers.
#ifndef RUNTIME_MTK_H
#define RUNTIME_MTK_H

#include <stdbool.h>
#include <stdint.h>
#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------- sched_mbf: everything that changes configuration or state ----------

#define SCHED_TOPIC_MAX 96
#define SCHED_TEXT_MAX  2048   // matches the MQTT payload limit

typedef enum {
    SCHED_CONSOLE_LINE = 1,    // a complete REPL line from ControlTask
    SCHED_MQTT_CONFIG,         // payload from pico/<node>/config or broadcast/config
    SCHED_MQTT_CMD,            // payload from pico/<node>/cmd
    SCHED_MQTT_NODE,           // payload from pico/<node>/in
    SCHED_REMOTE_LINE,         // a followed node's sensor line; topic says which
    SCHED_MODEL_PARAMS,        // pico/<node>/model/params (binary; len is set)
    SCHED_INFER_DONE,          // InferenceTask finished a run (task 31)
} sched_kind_t;

typedef struct {
    uint8_t  kind;             // sched_kind_t
    uint16_t len;              // payload length, for binary payloads
    char     topic[SCHED_TOPIC_MAX];
    char     text[SCHED_TEXT_MAX];
} sched_msg_t;

// ---------- pub_mbf: everything MQTTTask publishes ----------

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

// ---------- bench_mbf: timing records printed by BenchmarkTask ----------

typedef struct {
    char     name[24];
    uint32_t value_us;
} bench_msg_t;

// Message-buffer capacities, in messages: the Week 6 queue depths.
#define SCHED_MBF_MSGS  6
#define PUB_MBF_MSGS    16
#define INFER_MBF_MSGS  4
#define APP_MBF_MSGS    8
#define BENCH_MBF_MSGS  16
#define INFER_MSG_MAX   64     // infer_mbf message size (task 31 defines the content)
#define APP_MSG_MAX     64

// net_flg bits, set by MQTTTask and the network event handler
#define NET_WIFI_UP (1u << 0)
#define NET_MQTT_UP (1u << 1)

// Kernel object IDs, valid after runtime_start().
extern ID sched_mbf, pub_mbf, infer_mbf, app_mbf, bench_mbf;
extern ID net_flg;

// Sends to a message buffer without waiting (TMO_POL). When the buffer is
// full the newest message is dropped, counted per buffer and reported by
// TASKS; returns false in that case.
bool runtime_send(ID mbf, const void *msg, INT size);

// Wi-Fi and broker settings for MQTTTask. Call before runtime_start();
// without them the node runs offline.
void runtime_set_network(const char *ssid, const char *pass, const char *mqtt_host,
                         uint16_t mqtt_port, const char *node_id);
const char *runtime_node_id(void);
const char *runtime_broker_host(void);
uint16_t    runtime_broker_port(void);
const char *runtime_wifi_ssid(void);
const char *runtime_wifi_pass(void);

// Creates every kernel object, checks each result, then creates and starts
// the tasks. Call once from usermain after console_init(). Returns E_OK or
// the first failing error code (the failure is printed with the object name).
ER runtime_start(void);

// Prints every task's ID, core, priority, stack headroom and per-core
// residency, the message-buffer drop counters and the heap (console TASKS).
void runtime_print_tasks(void);

// Records a timing sample for BenchmarkTask without blocking the caller.
void runtime_bench(const char *name, uint32_t value_us);

// Registered by the MQTT layer (task 30); SchedulerTask calls them so that
// configuration is only ever applied in that task.
void runtime_set_mqtt_handlers(void (*cfg)(const char *), void (*cmd)(const char *),
                               void (*node)(const char *, const char *));

// Every application task calls this once per loop: it records which core the
// task ran on (the evidence for the core assignment) and its heartbeat for
// MonitorTask.
typedef enum {
    RT_SCHEDULER = 0, RT_MQTT, RT_CONTROL, RT_INFERENCE, RT_APPLICATION,
    RT_BENCHMARK, RT_MONITOR, RT_NTASKS
} rt_task_t;
void runtime_alive(rt_task_t t);

#ifdef __cplusplus
}
#endif

#endif // RUNTIME_MTK_H
