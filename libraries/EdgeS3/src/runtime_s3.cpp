// runtime_s3.cpp
// FreeRTOS runtime for the ESP32-S3 (D1 task 13). Tasks, priorities, cores and
// queues follow the Week 6 design review (section 3):
//
//   SchedulerTask   p4 core 1  owns configuration, sensors, rules; sched_q
//   MQTTTask        p3 core 0  Wi-Fi and MQTT (mqtt_s3.cpp); pub_q
//   ControlTask     p2 core 1  USB console lines to sched_q
//   InferenceTask   p2 core 1  model storage and inference (task 20); infer_q
//   ApplicationTask p2 core 1  application logic (D6); app_q
//   BenchmarkTask   p1 core 1  prints timing records; bench_q
//   MonitorTask     Arduino loop(), p1 core 1: stack, heap and watchdog checks
//
// The task watchdog (esp_task_wdt, a documented ESP-IDF exception) covers
// SchedulerTask and ControlTask; both wake at least once a second.
#include <Arduino.h>
#include <string.h>
#include "esp_task_wdt.h"

#include <EdgeCore.h>
#include "mqtt/sensor_bridge.h"
#include "mqtt/mqtt_model_transfer.h"
#include "runtime_s3.h"

extern "C" void control_bind_flags(bool *running, bool *cfg_ready);
extern "C" bool control_mqtt_start(const char *broker, uint16_t port, const char *client_id, const char *node_id);
extern "C" void mqtt_task_body(void);
extern "C" void mqtt_wifi_scan(void);
extern "C" void mqtt_wifi_drop(void);
extern "C" const char *runtime_node_id(void);
extern "C" const char *runtime_broker_host(void);
extern "C" uint16_t runtime_broker_port(void);

QueueHandle_t sched_q;
QueueHandle_t pub_q;
QueueHandle_t infer_q;
QueueHandle_t app_q;
QueueHandle_t bench_q;
EventGroupHandle_t net_events;

static TaskHandle_t h_sched, h_mqtt, h_control, h_infer, h_app, h_bench;

// MQTT callbacks registered by the S3 MQTT layer (task 19); SchedulerTask
// calls them so that configuration is only ever applied in this task.
static void (*s_cfg_cb)(const char *);
static void (*s_cmd_cb)(const char *);
static void (*s_node_cb)(const char *, const char *);

extern "C" void runtime_set_mqtt_handlers(void (*cfg)(const char *), void (*cmd)(const char *),
                                          void (*node)(const char *, const char *)) {
    s_cfg_cb = cfg;
    s_cmd_cb = cmd;
    s_node_cb = node;
}

// "pico/<node>/sensor/<source>/line" -> node, source
static bool parse_line_topic(const char *topic, char *node, size_t nn, char *src, size_t sn) {
    if (strncmp(topic, "pico/", 5) != 0) return false;
    const char *n0 = topic + 5;
    const char *n1 = strchr(n0, '/');
    if (!n1 || strncmp(n1, "/sensor/", 8) != 0) return false;
    const char *s0 = n1 + 8;
    const char *s1 = strchr(s0, '/');
    if (!s1 || strcmp(s1, "/line") != 0) return false;
    snprintf(node, nn, "%.*s", (int)(n1 - n0), n0);
    snprintf(src, sn, "%.*s", (int)(s1 - s0), s0);
    return true;
}

// ---------- SchedulerTask ----------

static void handle_sched_msg(sched_msg_t *m, bool *running, bool *cfg_ready) {
    switch (m->kind) {
    case SCHED_CONSOLE_LINE:
        repl_handle_line(m->text, running, cfg_ready);
        break;
    case SCHED_MQTT_CONFIG:
        if (s_cfg_cb) s_cfg_cb(m->text);
        break;
    case SCHED_MQTT_CMD:
        if (s_cmd_cb) s_cmd_cb(m->text);
        break;
    case SCHED_MQTT_NODE:
        if (s_node_cb) s_node_cb("unknown", m->text);
        break;
    case SCHED_MODEL_PARAMS:
        inference_mgr_set_params((const uint8_t *)m->text, m->len);
        break;
    case SCHED_REMOTE_LINE: {
        char node[32], src[32];
        if (parse_line_topic(m->topic, node, sizeof node, src, sizeof src))
            sensor_bridge_on_remote_line(node, src, m->text, hal_time_ms());
        break;
    }
    default:
        break;
    }
}

// SchedulerTask cycle time: the work done on each wake-up (messages, sensor
// reads, rules, model and inference polling), for WCET evidence (D5). Only
// SchedulerTask writes the statistics; TASKS prints them and asks for a reset.
static const uint32_t k_cycle_edges_us[] = { 100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000 };
static const size_t k_cycle_bins = sizeof k_cycle_edges_us / sizeof k_cycle_edges_us[0] + 1;
static uint32_t s_cycle_hist[sizeof k_cycle_edges_us / sizeof k_cycle_edges_us[0] + 1];
static uint32_t s_cycle_n, s_cycle_max_us;
static uint64_t s_cycle_sum_us;
static volatile bool s_cycle_reset;

static void cycle_record(uint32_t us) {
    if (s_cycle_reset) {
        memset(s_cycle_hist, 0, sizeof s_cycle_hist);
        s_cycle_n = 0; s_cycle_max_us = 0; s_cycle_sum_us = 0;
        s_cycle_reset = false;
    }
    size_t b = 0;
    while (b < k_cycle_bins - 1 && us >= k_cycle_edges_us[b]) b++;
    s_cycle_hist[b]++;
    s_cycle_n++;
    s_cycle_sum_us += us;
    if (us > s_cycle_max_us) s_cycle_max_us = us;
}

static void cycle_print_and_reset(void) {
    uint32_t n = s_cycle_n, need = (uint32_t)((n * 95ULL + 99) / 100), seen = 0;
    size_t b = 0;
    for (; b < k_cycle_bins && n; b++) { seen += s_cycle_hist[b]; if (seen >= need) break; }
    char p95[16];
    if (!n) snprintf(p95, sizeof p95, "-");
    else if (b >= k_cycle_bins - 1) snprintf(p95, sizeof p95, ">%u", (unsigned)k_cycle_edges_us[k_cycle_bins - 2]);
    else snprintf(p95, sizeof p95, "<%u", (unsigned)k_cycle_edges_us[b]);
    printf("[TASK] SchedulerTask cycles=%u mean_us=%u p95_us=%s max_us=%u\n", (unsigned)n,
           n ? (unsigned)(s_cycle_sum_us / n) : 0u, p95, (unsigned)s_cycle_max_us);
    s_cycle_reset = true;
}

static void scheduler_task(void *arg) {
    (void)arg;
    static bool running = false, cfg_ready = false;
    static sched_msg_t msg;

    app_cfg_t defaults;
    repl_init_defaults(&defaults);
    scheduler_reset();
    control_bind_flags(&running, &cfg_ready);

    model_transfer_init();
    inference_mgr_init();
    inference_mgr_load_base_model();

    // Same start-up as the Pico's main.c; MQTTTask connects once Wi-Fi is up.
    if (runtime_broker_host()[0])
        control_mqtt_start(runtime_broker_host(), runtime_broker_port(), runtime_node_id(), runtime_node_id());

    esp_task_wdt_add(NULL);
    for (;;) {
        uint32_t wait_ms = scheduler_next_due_ms(running, 1000);
        bool got = xQueueReceive(sched_q, &msg, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
        uint64_t t0 = hal_time_us();
        if (got) handle_sched_msg(&msg, &running, &cfg_ready);
        scheduler_run_all(running);
        model_transfer_poll();
        inference_mgr_poll();
        cycle_record((uint32_t)(hal_time_us() - t0));
        esp_task_wdt_reset();
    }
}

// ---------- ControlTask ----------

static void control_task(void *arg) {
    (void)arg;
    static char line[512];
    static size_t len = 0;
    static sched_msg_t msg;

    esp_task_wdt_add(NULL);
    for (;;) {
        int ch = hal_console_getc(100000);
        esp_task_wdt_reset();
        if (ch < 0 || !repl_line_feed(ch, line, sizeof line, &len)) continue;

        if (strcasecmp(line, "TASKS") == 0) {   // platform commands, not part of the REPL
            runtime_print_tasks();
        } else if (strcasecmp(line, "WIFISCAN") == 0) {
            mqtt_wifi_scan();
        } else if (strcasecmp(line, "WIFIDROP") == 0) {
            mqtt_wifi_drop();
        } else {
            msg.kind = SCHED_CONSOLE_LINE;
            msg.topic[0] = 0;
            snprintf(msg.text, sizeof msg.text, "%s", line);
            if (xQueueSend(sched_q, &msg, pdMS_TO_TICKS(100)) != pdTRUE)
                printf("[CTRL] scheduler busy, line dropped\n");
        }
        len = 0;
    }
}

// ---------- MQTTTask (body in mqtt_s3.cpp) ----------

static void mqtt_task(void *arg) {
    (void)arg;
    mqtt_task_body();
}

// ---------- InferenceTask (task 20 replaces the body) ----------

static void inference_task(void *arg) {
    (void)arg;
    static uint8_t item[64];
    for (;;) xQueueReceive(infer_q, item, portMAX_DELAY);
}

// ---------- ApplicationTask (D6 application logic) ----------

static void application_task(void *arg) {
    (void)arg;
    static uint8_t item[64];
    for (;;) xQueueReceive(app_q, item, portMAX_DELAY);
}

// ---------- BenchmarkTask ----------

static void benchmark_task(void *arg) {
    (void)arg;
    bench_msg_t b;
    for (;;) {
        if (xQueueReceive(bench_q, &b, portMAX_DELAY) == pdTRUE)
            printf("[BENCH] %s %lu us\n", b.name, (unsigned long)b.value_us);
    }
}

extern "C" void runtime_bench(const char *name, uint32_t value_us) {
    if (!bench_q) return;
    bench_msg_t b;
    snprintf(b.name, sizeof b.name, "%s", name);
    b.value_us = value_us;
    xQueueSend(bench_q, &b, 0);
}

// ---------- start-up ----------

extern "C" void runtime_start(void) {
    sched_q   = xQueueCreate(6, sizeof(sched_msg_t));
    pub_q     = xQueueCreate(16, sizeof(pub_msg_t));
    infer_q   = xQueueCreate(4, 64);
    app_q     = xQueueCreate(8, 64);
    bench_q   = xQueueCreate(16, sizeof(bench_msg_t));
    net_events = xEventGroupCreate();

    xTaskCreatePinnedToCore(scheduler_task,   "SchedulerTask",   16384, NULL, 4, &h_sched,   1);
    xTaskCreatePinnedToCore(mqtt_task,        "MQTTTask",         8192, NULL, 3, &h_mqtt,    0);
    xTaskCreatePinnedToCore(control_task,     "ControlTask",      4096, NULL, 2, &h_control, 1);
    xTaskCreatePinnedToCore(inference_task,   "InferenceTask",    8192, NULL, 2, &h_infer,   1);
    xTaskCreatePinnedToCore(application_task, "ApplicationTask",  4096, NULL, 2, &h_app,     1);
    xTaskCreatePinnedToCore(benchmark_task,   "BenchmarkTask",    3072, NULL, 1, &h_bench,   1);
}

// ---------- MonitorTask (Arduino loop) ----------

static const struct { const char *name; TaskHandle_t *h; } k_tasks[] = {
    { "SchedulerTask", &h_sched }, { "MQTTTask", &h_mqtt }, { "ControlTask", &h_control },
    { "InferenceTask", &h_infer }, { "ApplicationTask", &h_app }, { "BenchmarkTask", &h_bench },
};

extern "C" void runtime_print_tasks(void) {
    for (const auto &t : k_tasks) {
        if (*t.h) printf("[TASK] %-16s stack_free=%u B\n", t.name, (unsigned)uxTaskGetStackHighWaterMark(*t.h));
    }
    printf("[TASK] heap_free=%u B min_free=%u B psram_free=%u B\n",
           (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getFreePsram());
    cycle_print_and_reset();
}

extern "C" void runtime_monitor_tick(void) {
    static bool warned[sizeof k_tasks / sizeof k_tasks[0]];
    for (size_t i = 0; i < sizeof k_tasks / sizeof k_tasks[0]; i++) {
        TaskHandle_t h = *k_tasks[i].h;
        if (!h) continue;
        unsigned free_b = (unsigned)uxTaskGetStackHighWaterMark(h);
        if (free_b < 1024 && !warned[i]) {
            printf("[MON] %s stack low: %u B free\n", k_tasks[i].name, free_b);
            warned[i] = true;
        }
    }
}
