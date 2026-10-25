// runtime_mtk.c
// µT-Kernel 3 SMP runtime for the ESP32-S3 (D1 task 28). It replaces the
// FreeRTOS runtime (libraries/EdgeS3/src/runtime_s3.cpp) with the same tasks,
// message formats and behaviour; MTK_REDESIGN.md sections 3-5 give the
// mapping and the reasons.
//
//   task              core  pri  FreeRTOS pri  waits on
//   SchedulerTask      1    21   4             sched_mbf, until the next sensor is due
//   MQTTTask           0    22   3             pub_mbf (10 ms) and the network
//   ControlTask        1    23   2             USB console receive flag
//   InferenceTask      1    23   2             infer_mbf
//   ApplicationTask    1    23   2             app_mbf (D6 application logic)
//   BenchmarkTask      1    24   1             bench_mbf
//   MonitorTask        1    24   1 (loop())    wake-up from a 1 s cyclic handler
//   idle (x2)        0, 1   32                 never blocks
//
// µT-Kernel priority = 25 - FreeRTOS priority: lower number is higher
// priority, and 25 is the base the course shim uses for IDF's own tasks, so
// the order between these tasks and the Wi-Fi, lwIP and MQTT tasks is the one
// the Week 6 design had. Every task is pinned at creation (TA_ASSPRC, one
// processor bit) and has TA_FPU: the rule engine, inference and printing use
// the FPU, and the port only allows coprocessor tasks that are pinned. Stacks
// and message-buffer storage are static (TA_USERBUF), so the kernel's object
// heap is not used for them, and every object is created and checked before
// any task starts.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <tk/tkernel.h>
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_private/periph_ctrl.h"
#include "hal/mwdt_ll.h"
#include "hal/timg_ll.h"
#include "hal/wdt_hal.h"

#include "hal/hal.h"
#include "application/scheduler.h"
#include "bus/bus_common.h"
#include "rule_engine/rules.h"
#include "inference_engine/inference_manager.h"
#include "mqtt/sensor_bridge.h"
#include "mqtt/mqtt_model_transfer.h"
#include "edge_mtk.h"
#include "runtime_mtk.h"

// Defined in the shared code (mqtt/mqtt_config_parser.c) and by the MQTT layer.
void control_bind_flags(bool *running, bool *cfg_ready);
bool control_mqtt_start(const char *broker, uint16_t port, const char *client_id, const char *node_id);
void mqtt_task_body(void);
void mqtt_wifi_scan(void);
void mqtt_wifi_drop(void);
void inference_task_body(void);
void mtk_spi_benchmark(unsigned mode, uint32_t hz);
void mtk_spi_flash_id(void);
void mtk_spi_flash_check(uint32_t expected_id);
void mtk_spi_flash_scan(uint32_t expected_id);
void mtk_spi_flash_program(uint32_t expected_id);
void mtk_spi_flash_verify(uint32_t expected_id);

ID sched_mbf, pub_mbf, infer_mbf, app_mbf, bench_mbf;
ID net_flg;

// ---------- network settings ----------

static char s_ssid[33], s_pass[65], s_host[64], s_node[64] = "s3-001";
static uint16_t s_port = 1883;

void runtime_set_network(const char *ssid, const char *pass, const char *mqtt_host,
                         uint16_t mqtt_port, const char *node_id)
{
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid ? ssid : "");
    snprintf(s_pass, sizeof s_pass, "%s", pass ? pass : "");
    snprintf(s_host, sizeof s_host, "%s", mqtt_host ? mqtt_host : "");
    s_port = mqtt_port ? mqtt_port : 1883;
    snprintf(s_node, sizeof s_node, "%s", (node_id && *node_id) ? node_id : "s3-001");
}

const char *runtime_node_id(void)     { return s_node; }
const char *runtime_broker_host(void) { return s_host; }
uint16_t    runtime_broker_port(void) { return s_port; }
const char *runtime_wifi_ssid(void)   { return s_ssid; }
const char *runtime_wifi_pass(void)   { return s_pass; }

// ---------- task table ----------

#define STACK_FILL 0xA5

typedef struct {
    const char *name;
    FP          entry;
    PRI         pri;
    int         core;       // 0 or 1
    SZ          stack;
    uint8_t    *buf;
    ID          id;
} rt_task_def_t;

static void scheduler_task(INT, void *);
static void mqtt_task(INT, void *);
static void control_task(INT, void *);
static void inference_task(INT, void *);
static void application_task(INT, void *);
static void benchmark_task(INT, void *);
static void monitor_task(INT, void *);

static uint8_t s_stk_sched[16384] __attribute__((aligned(16)));
// MQTTTask runs esp-mqtt's publish and lwIP's send path in its own context:
// measured at about 7.7 KB on the first publish, so it gets 16 KB.
static uint8_t s_stk_mqtt[16384]  __attribute__((aligned(16)));
static uint8_t s_stk_ctrl[4096]   __attribute__((aligned(16)));
static uint8_t s_stk_infer[8192]  __attribute__((aligned(16)));
static uint8_t s_stk_app[4096]    __attribute__((aligned(16)));
static uint8_t s_stk_bench[3072]  __attribute__((aligned(16)));
static uint8_t s_stk_mon[4096]    __attribute__((aligned(16)));
static uint8_t s_stk_idle[2][1024] __attribute__((aligned(16)));

static rt_task_def_t s_tasks[RT_NTASKS] = {
    [RT_SCHEDULER]   = { "SchedulerTask",   (FP)scheduler_task,   21, 1, sizeof s_stk_sched, s_stk_sched, 0 },
    [RT_MQTT]        = { "MQTTTask",        (FP)mqtt_task,        22, 0, sizeof s_stk_mqtt,  s_stk_mqtt,  0 },
    [RT_CONTROL]     = { "ControlTask",     (FP)control_task,     23, 1, sizeof s_stk_ctrl,  s_stk_ctrl,  0 },
    [RT_INFERENCE]   = { "InferenceTask",   (FP)inference_task,   23, 1, sizeof s_stk_infer, s_stk_infer, 0 },
    [RT_APPLICATION] = { "ApplicationTask", (FP)application_task, 23, 1, sizeof s_stk_app,   s_stk_app,   0 },
    [RT_BENCHMARK]   = { "BenchmarkTask",   (FP)benchmark_task,   24, 1, sizeof s_stk_bench, s_stk_bench, 0 },
    [RT_MONITOR]     = { "MonitorTask",     (FP)monitor_task,     24, 1, sizeof s_stk_mon,   s_stk_mon,   0 },
};

// Per-task residency (how many loop iterations ran on each core) and the time
// of the last heartbeat. Each task writes only its own row.
static volatile uint32_t s_core_hits[RT_NTASKS][2];
static volatile uint32_t s_alive_ms[RT_NTASKS];

void runtime_alive(rt_task_t t)
{
    ID prc = tk_get_prc();                    // 1 = core 0, 2 = core 1
    if (prc == 1 || prc == 2) s_core_hits[t][prc - 1]++;
    s_alive_ms[t] = hal_time_ms();
}

// ---------- message buffers ----------

// µT-Kernel stores each message with a 4-byte header, rounded up to 4 bytes.
#define MBF_BYTES(n, sz) ((n) * (4 + (((sz) + 3) & ~3)))

static uint32_t s_mbf_sched[MBF_BYTES(SCHED_MBF_MSGS, sizeof(sched_msg_t)) / 4];
static uint32_t s_mbf_pub[MBF_BYTES(PUB_MBF_MSGS, sizeof(pub_msg_t)) / 4];
static uint32_t s_mbf_infer[MBF_BYTES(INFER_MBF_MSGS, INFER_MSG_MAX) / 4];
static uint32_t s_mbf_app[MBF_BYTES(APP_MBF_MSGS, APP_MSG_MAX) / 4];
static uint32_t s_mbf_bench[MBF_BYTES(BENCH_MBF_MSGS, sizeof(bench_msg_t)) / 4];

static struct { const char *name; ID *id; volatile uint32_t dropped; } s_mbfs[] = {
    { "sched_mbf", &sched_mbf, 0 }, { "pub_mbf", &pub_mbf, 0 }, { "infer_mbf", &infer_mbf, 0 },
    { "app_mbf", &app_mbf, 0 },     { "bench_mbf", &bench_mbf, 0 },
};

bool runtime_send(ID mbf, const void *msg, INT size)
{
    if (mbf <= 0) return false;
    ER er = tk_snd_mbf(mbf, (void *)msg, size, TMO_POL);
    if (er == E_OK) return true;
    for (size_t i = 0; i < sizeof s_mbfs / sizeof s_mbfs[0]; i++)
        if (*s_mbfs[i].id == mbf) __atomic_add_fetch(&s_mbfs[i].dropped, 1, __ATOMIC_RELAXED);
    return false;
}

// ---------- MQTT handlers (task 30 registers them) ----------

static void (*s_cfg_cb)(const char *);
static void (*s_cmd_cb)(const char *);
static void (*s_node_cb)(const char *, const char *);

void runtime_set_mqtt_handlers(void (*cfg)(const char *), void (*cmd)(const char *),
                               void (*node)(const char *, const char *))
{
    s_cfg_cb = cfg;
    s_cmd_cb = cmd;
    s_node_cb = node;
}

// "pico/<node>/sensor/<source>/line" -> node, source
static bool parse_line_topic(const char *topic, char *node, size_t nn, char *src, size_t sn)
{
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

static void handle_sched_msg(sched_msg_t *m, bool *running, bool *cfg_ready)
{
    switch (m->kind) {
    case SCHED_CONSOLE_LINE: repl_handle_line(m->text, running, cfg_ready); break;
    case SCHED_MQTT_CONFIG:  if (s_cfg_cb) s_cfg_cb(m->text); break;
    case SCHED_MQTT_CMD:     if (s_cmd_cb) s_cmd_cb(m->text); break;
    case SCHED_MQTT_NODE:    if (s_node_cb) s_node_cb("unknown", m->text); break;
    case SCHED_MODEL_PARAMS: inference_mgr_set_params((const uint8_t *)m->text, m->len); break;
    case SCHED_REMOTE_LINE: {
        char node[32], src[32];
        if (parse_line_topic(m->topic, node, sizeof node, src, sizeof src))
            sensor_bridge_on_remote_line(node, src, m->text, hal_time_ms());
        break;
    }
    default: break;
    }
}

// SchedulerTask cycle time: the work done on each wake-up (messages, sensor
// reads, rules, model and inference polling), for WCET evidence. Only
// SchedulerTask writes the statistics; TASKS prints them and asks for a reset.
static const uint32_t k_cycle_edges_us[] = { 100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000 };
#define CYCLE_BINS (sizeof k_cycle_edges_us / sizeof k_cycle_edges_us[0] + 1)
static uint32_t s_cycle_hist[CYCLE_BINS];
static uint32_t s_cycle_n, s_cycle_max_us;
static uint64_t s_cycle_sum_us;
static volatile bool s_cycle_reset;

static void cycle_record(uint32_t us)
{
    if (s_cycle_reset) {
        memset(s_cycle_hist, 0, sizeof s_cycle_hist);
        s_cycle_n = 0; s_cycle_max_us = 0; s_cycle_sum_us = 0;
        s_cycle_reset = false;
    }
    size_t b = 0;
    while (b < CYCLE_BINS - 1 && us >= k_cycle_edges_us[b]) b++;
    s_cycle_hist[b]++;
    s_cycle_n++;
    s_cycle_sum_us += us;
    if (us > s_cycle_max_us) s_cycle_max_us = us;
}

static void cycle_print_and_reset(void)
{
    uint32_t n = s_cycle_n, need = (uint32_t)((n * 95ULL + 99) / 100), seen = 0;
    size_t b = 0;
    for (; b < CYCLE_BINS && n; b++) { seen += s_cycle_hist[b]; if (seen >= need) break; }
    char p95[16];
    if (!n) snprintf(p95, sizeof p95, "-");
    else if (b >= CYCLE_BINS - 1) snprintf(p95, sizeof p95, ">%u", (unsigned)k_cycle_edges_us[CYCLE_BINS - 2]);
    else snprintf(p95, sizeof p95, "<%u", (unsigned)k_cycle_edges_us[b]);
    printf("[TASK] SchedulerTask cycles=%u mean_us=%u p95_us=%s max_us=%u\n", (unsigned)n,
           n ? (unsigned)(s_cycle_sum_us / n) : 0u, p95, (unsigned)s_cycle_max_us);
    s_cycle_reset = true;
}

static void scheduler_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    static bool running = false, cfg_ready = false;
    static sched_msg_t msg;

    // The GPIO interrupt and the PULSE alarm handlers belong to this task's
    // core (core 1), so the HAL is started here.
    ER er = hal_mtk_init();
    if (er != E_OK) printf("[SCHED] hal_mtk_init failed er=%d\n", (int)er);

    app_cfg_t defaults;
    repl_init_defaults(&defaults);
    scheduler_reset();
    control_bind_flags(&running, &cfg_ready);

    model_transfer_init();
    inference_mgr_init();
    inference_mgr_load_base_model();

    // Same start-up as the Pico's main.c; MQTTTask connects once Wi-Fi is up.
    if (s_host[0]) control_mqtt_start(s_host, s_port, s_node, s_node);

    for (;;) {
        uint32_t wait_ms = scheduler_next_due_ms(running, 1000);
        INT got = tk_rcv_mbf(sched_mbf, &msg, (TMO)wait_ms);
        uint64_t t0 = hal_time_us();
        if (got > 0) handle_sched_msg(&msg, &running, &cfg_ready);
        scheduler_run_all(running);
        model_transfer_poll();
        inference_mgr_poll();
        cycle_record((uint32_t)(hal_time_us() - t0));
        runtime_alive(RT_SCHEDULER);
    }
}

// ---------- ControlTask ----------

static void control_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    static char line[512];
    static size_t len = 0;
    static sched_msg_t msg;

    ER er = console_start_rx();               // USB receive interrupt on this core
    if (er != E_OK) printf("[CTRL] console_start_rx failed er=%d; polling instead\n", (int)er);

    for (;;) {
        int ch = hal_console_getc(100000);
        runtime_alive(RT_CONTROL);
        if (ch < 0 || !repl_line_feed(ch, line, sizeof line, &len)) continue;

        if (strcasecmp(line, "TASKS") == 0) {   // platform commands, not part of the REPL
            runtime_print_tasks();
        } else if (strcasecmp(line, "WIFISCAN") == 0) {
            mqtt_wifi_scan();
        } else if (strcasecmp(line, "WIFIDROP") == 0) {
            mqtt_wifi_drop();
        } else if (strcasecmp(line, "SPIID") == 0) {
            bench_msg_t b = { .name = "SPIID" };
            if (!runtime_send(bench_mbf, &b, sizeof b))
                puts("[SPIID] ERROR benchmark queue full");
        } else if (strncasecmp(line, "SPICHECK ", 9) == 0 ||
                   strncasecmp(line, "SPISCAN ", 8) == 0 ||
                   strncasecmp(line, "SPIPROGRAM ", 11) == 0 ||
                   strncasecmp(line, "SPIVERIFY ", 10) == 0) {
            unsigned long expected;
            char extra;
            const bool scan = strncasecmp(line, "SPISCAN ", 8) == 0;
            const bool program = strncasecmp(line, "SPIPROGRAM ", 11) == 0;
            const bool verify = strncasecmp(line, "SPIVERIFY ", 10) == 0;
            const char *value = line + (scan ? 8 : program ? 11 : verify ? 10 : 9);
            if (sscanf(value, "%lx %c", &expected, &extra) != 1 ||
                expected == 0 || expected >= 0xfffffful) {
                puts("[SPI] ERROR expected <SPI command> <six-digit JEDEC ID>");
            } else {
                bench_msg_t b = { .value_us = (uint32_t)expected };
                snprintf(b.name, sizeof b.name, "%s",
                         scan ? "SPISCAN" : program ? "SPIPROGRAM" :
                         verify ? "SPIVERIFY" : "SPICHECK");
                if (!runtime_send(bench_mbf, &b, sizeof b))
                    puts("[SPI] ERROR benchmark queue full");
            }
        } else if (strncasecmp(line, "SPIBENCH ", 9) == 0) {
            unsigned mode;
            unsigned long hz;
            char extra;
            if (sscanf(line, "SPIBENCH %u %lu %c", &mode, &hz, &extra) != 2 ||
                mode > 3 || hz < 1000000ul || hz > 20000000ul) {
                puts("[SPIBENCH] ERROR expected SPIBENCH <mode 0..3> <hz 1000000..20000000>");
            } else {
                bench_msg_t b = { .name = "SPIBENCH",
                                  .value_us = (uint32_t)hz | ((uint32_t)mode << 30) };
                if (!runtime_send(bench_mbf, &b, sizeof b))
                    puts("[SPIBENCH] ERROR benchmark queue full");
            }
        } else if (strncasecmp(line, "STALL ", 6) == 0) {
            // Watchdog test: this task stops reporting for the given time, as
            // if it were stuck. MonitorTask stops feeding the hardware watchdog
            // after 5 s and the chip resets WDT_TIMEOUT_S later.
            uint32_t ms = (uint32_t)strtoul(line + 6, NULL, 10);
            printf("[CTRL] stalling ControlTask for %u ms (watchdog test)\n", (unsigned)ms);
            tk_dly_tsk((TMO)ms);
            printf("[CTRL] stall over\n");
        } else {
            msg.kind = SCHED_CONSOLE_LINE;
            msg.len = 0;
            msg.topic[0] = 0;
            snprintf(msg.text, sizeof msg.text, "%s", line);
            // A full scheduler buffer is waited on for up to 100 ms, then the
            // line is dropped, as with the FreeRTOS queue.
            if (tk_snd_mbf(sched_mbf, &msg, sizeof msg, 100) != E_OK) {
                __atomic_add_fetch(&s_mbfs[0].dropped, 1, __ATOMIC_RELAXED);
                printf("[CTRL] scheduler busy, line dropped\n");
            }
        }
        len = 0;
    }
}

// ---------- MQTTTask (body in net_mtk.c, task 30) ----------

static void mqtt_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    mqtt_task_body();
}

// ---------- InferenceTask (body in inference_mtk.cpp: owns the interpreter) ----------

static void inference_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    inference_task_body();
}

// ---------- ApplicationTask (D6 application logic) ----------

static void application_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    static uint8_t item[APP_MSG_MAX];
    for (;;) {
        if (tk_rcv_mbf(app_mbf, item, TMO_FEVR) > 0) runtime_alive(RT_APPLICATION);
    }
}

// ---------- BenchmarkTask ----------

static void benchmark_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    bench_msg_t b;
    for (;;) {
        if (tk_rcv_mbf(bench_mbf, &b, TMO_FEVR) > 0) {
            runtime_alive(RT_BENCHMARK);
            if (strcmp(b.name, "SPIBENCH") == 0)
                mtk_spi_benchmark(b.value_us >> 30, b.value_us & 0x3fffffffu);
            else if (strcmp(b.name, "SPIID") == 0)
                mtk_spi_flash_id();
            else if (strcmp(b.name, "SPICHECK") == 0)
                mtk_spi_flash_check(b.value_us);
            else if (strcmp(b.name, "SPISCAN") == 0)
                mtk_spi_flash_scan(b.value_us);
            else if (strcmp(b.name, "SPIPROGRAM") == 0)
                mtk_spi_flash_program(b.value_us);
            else if (strcmp(b.name, "SPIVERIFY") == 0)
                mtk_spi_flash_verify(b.value_us);
            else
                printf("[BENCH] %s %lu us\n", b.name, (unsigned long)b.value_us);
        }
    }
}

void runtime_bench(const char *name, uint32_t value_us)
{
    bench_msg_t b;
    snprintf(b.name, sizeof b.name, "%s", name);
    b.value_us = value_us;
    runtime_send(bench_mbf, &b, sizeof b);
}

// ---------- stacks ----------

// Free bytes at the bottom of a task's stack that were never written. The
// stack grows down from the top; the lowest word holds the kernel's overflow
// canary and is skipped.
static uint32_t stack_free(const rt_task_def_t *t)
{
    const uint8_t *p = t->buf + 4, *end = t->buf + t->stack;
    while (p < end && *p == STACK_FILL) p++;
    return (uint32_t)(p - (t->buf + 4));
}

// ---------- MonitorTask ----------
// Woken every second by a cyclic handler. Reports stacks running low, checks
// the heartbeats of the tasks that must keep running, and feeds the hardware
// watchdog only while all of them are alive. IDF's task and interrupt
// watchdogs are unarmed in the course port's SMP profile, so this is the
// watchdog: a task stuck for longer than its limit stops the feeding and the
// timer-group-0 watchdog resets the chip WDT_TIMEOUT_S later.
//
// Timer group 0's watchdog (MWDT0), not the RTC watchdog: the RTC watchdog
// lives in the RTC power domain and survives the reset into the ROM download
// mode, so it would reset the chip in the middle of flashing a large image.
// A chip reset clears MWDT0.

#define WDT_TIMEOUT_S 10

static ID s_mon_cyc;
static wdt_hal_context_t s_wdt;
static bool s_wdt_on;

static const struct { rt_task_t task; uint32_t limit_ms; } k_supervised[] = {
    { RT_SCHEDULER, 5000 },   // waits at most 1 s for its next sensor
    { RT_CONTROL,   5000 },   // console read times out every 100 ms
    { RT_MQTT,     20000 },   // 10 ms loop; a Wi-Fi scan can hold it for seconds
};

static void monitor_cyclic(void *exinf)
{
    (void)exinf;
    tk_wup_tsk(s_tasks[RT_MONITOR].id);       // task-independent: wake only
}

static void wdt_start(void)
{
    PERIPH_RCC_ACQUIRE_ATOMIC(PERIPH_TIMG0_MODULE, ref_count) {
        if (ref_count == 0) {
            timg_ll_enable_bus_clock(0, true);
            timg_ll_reset_register(0);
        }
    }
    // 80 MHz / 40000 = 500 us per tick
    wdt_hal_init(&s_wdt, WDT_MWDT0, MWDT_LL_DEFAULT_CLK_PRESCALER, false);
    wdt_hal_write_protect_disable(&s_wdt);
    wdt_hal_config_stage(&s_wdt, WDT_STAGE0, WDT_TIMEOUT_S * 2000, WDT_STAGE_ACTION_RESET_SYSTEM);
    wdt_hal_enable(&s_wdt);
    wdt_hal_write_protect_enable(&s_wdt);
    s_wdt_on = true;
}

static void wdt_feed(void)
{
    wdt_hal_write_protect_disable(&s_wdt);
    wdt_hal_feed(&s_wdt);
    wdt_hal_write_protect_enable(&s_wdt);
}

static void monitor_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    static bool warned_stack[RT_NTASKS], warned_alive[RT_NTASKS];
    wdt_start();
    printf("[MON] hardware watchdog armed: %d s, fed while SchedulerTask, ControlTask and MQTTTask are alive\n",
           WDT_TIMEOUT_S);
    for (;;) {
        tk_slp_tsk(TMO_FEVR);                 // the cyclic handler wakes this task once a second
        runtime_alive(RT_MONITOR);
        uint32_t now = hal_time_ms();
        bool all_alive = true;
        for (size_t i = 0; i < sizeof k_supervised / sizeof k_supervised[0]; i++) {
            rt_task_t t = k_supervised[i].task;
            uint32_t silent = now - s_alive_ms[t];
            if (s_alive_ms[t] && silent > k_supervised[i].limit_ms) {
                all_alive = false;
                if (!warned_alive[t])
                    printf("[MON] %s silent for %u ms: watchdog no longer fed\n", s_tasks[t].name, (unsigned)silent);
                warned_alive[t] = true;
            } else {
                warned_alive[t] = false;
            }
        }
        if (all_alive && s_wdt_on) wdt_feed();
        for (int t = 0; t < RT_NTASKS; t++) {
            uint32_t free_b = stack_free(&s_tasks[t]);
            if (free_b < 1024 && !warned_stack[t]) {
                printf("[MON] %s stack low: %u B free\n", s_tasks[t].name, (unsigned)free_b);
                warned_stack[t] = true;
            }
        }
    }
}

// ---------- TASKS ----------

void runtime_print_tasks(void)
{
    for (int t = 0; t < RT_NTASKS; t++) {
        const rt_task_def_t *d = &s_tasks[t];
        T_RTSK r;
        PRI pri = tk_ref_tsk(d->id, &r) == E_OK ? r.tskpri : -1;
        printf("[TASK] %-16s id=%-2d core=%d pri=%d stack_free=%u B ran_on_core0=%u ran_on_core1=%u\n",
               d->name, (int)d->id, d->core, (int)pri, (unsigned)stack_free(d),
               (unsigned)s_core_hits[t][0], (unsigned)s_core_hits[t][1]);
    }
    for (size_t i = 0; i < sizeof s_mbfs / sizeof s_mbfs[0]; i++) {
        T_RMBF r;
        int free_b = tk_ref_mbf(*s_mbfs[i].id, &r) == E_OK ? (int)r.frbufsz : -1;
        printf("[TASK] %-10s free=%d B dropped=%u\n", s_mbfs[i].name, free_b, (unsigned)s_mbfs[i].dropped);
    }
    uint32_t irqs, rx_drop;
    console_stats(&irqs, &rx_drop);
    printf("[TASK] heap_free=%u B min_free=%u B largest=%u B console_rx_irqs=%u console_rx_dropped=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)irqs, (unsigned)rx_drop);
    cycle_print_and_reset();
}

// ---------- start-up ----------

static void idle_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    for (;;) asm volatile ("waiti 0" ::: "memory");
}

#define CHECK(what, expr) do {                                              \
        ER _er = (ER)(expr);                                                \
        if (_er < E_OK) {                                                   \
            printf("[BOOT] %s failed: er=%d\n", what, (int)_er);           \
            return _er;                                                     \
        }                                                                   \
    } while (0)

static ER make_mbf(ID *id, void *buf, SZ bufsz, INT maxmsz, const char *name)
{
    T_CMBF c = { .exinf = NULL, .mbfatr = TA_TFIFO | TA_USERBUF, .bufsz = bufsz,
                 .maxmsz = maxmsz, .bufptr = buf };
    ID r = tk_cre_mbf(&c);
    CHECK(name, r);
    *id = r;
    return E_OK;
}

static ER make_task(rt_task_def_t *t, ATR extra, UW prc)
{
    memset(t->buf, STACK_FILL, t->stack);
    T_CTSK c = { .exinf = NULL, .tskatr = TA_HLNG | TA_RNG0 | TA_USERBUF | TA_ASSPRC | extra,
                 .task = t->entry, .itskpri = t->pri, .stksz = t->stack, .bufptr = t->buf,
                 .assprc = prc };
    ID id = tk_cre_tsk(&c);
    CHECK(t->name, id);
    t->id = id;
    return E_OK;
}

ER runtime_start(void)
{
    CHECK("sched_mbf", make_mbf(&sched_mbf, s_mbf_sched, sizeof s_mbf_sched, sizeof(sched_msg_t), "sched_mbf"));
    CHECK("pub_mbf",   make_mbf(&pub_mbf,   s_mbf_pub,   sizeof s_mbf_pub,   sizeof(pub_msg_t),   "pub_mbf"));
    CHECK("infer_mbf", make_mbf(&infer_mbf, s_mbf_infer, sizeof s_mbf_infer, INFER_MSG_MAX,       "infer_mbf"));
    CHECK("app_mbf",   make_mbf(&app_mbf,   s_mbf_app,   sizeof s_mbf_app,   APP_MSG_MAX,         "app_mbf"));
    CHECK("bench_mbf", make_mbf(&bench_mbf, s_mbf_bench, sizeof s_mbf_bench, sizeof(bench_msg_t), "bench_mbf"));

    T_CFLG cflg = { .exinf = NULL, .flgatr = TA_TFIFO | TA_WMUL, .iflgptn = 0 };
    ID flg = tk_cre_flg(&cflg);
    CHECK("net_flg", flg);
    net_flg = flg;
    CHECK("inference reply buffer and mutex", inference_mtk_init());

    // Idle tasks first, one per processor, so neither dispatcher is ever left
    // without a ready task.
    static rt_task_def_t idle[2] = {
        { "idle0", (FP)idle_task, CNF_MAX_TSKPRI, 0, sizeof s_stk_idle[0], s_stk_idle[0], 0 },
        { "idle1", (FP)idle_task, CNF_MAX_TSKPRI, 1, sizeof s_stk_idle[1], s_stk_idle[1], 0 },
    };
    for (int i = 0; i < 2; i++) {
        CHECK(idle[i].name, make_task(&idle[i], 0, i == 0 ? TP_PRC1 : TP_PRC2));
        CHECK(idle[i].name, tk_sta_tsk(idle[i].id, 0));
    }

    for (int t = 0; t < RT_NTASKS; t++)
        CHECK(s_tasks[t].name, make_task(&s_tasks[t], TA_FPU, s_tasks[t].core == 0 ? TP_PRC1 : TP_PRC2));

    T_CCYC ccyc = { .exinf = NULL, .cycatr = TA_HLNG | TA_STA, .cychdr = (FP)monitor_cyclic,
                    .cyctim = 1000, .cycphs = 1000 };
    ID cyc = tk_cre_cyc(&ccyc);
    CHECK("monitor cyclic handler", cyc);
    s_mon_cyc = cyc;

    for (int t = 0; t < RT_NTASKS; t++) CHECK(s_tasks[t].name, tk_sta_tsk(s_tasks[t].id, 0));

    printf("[BOOT] micro T-Kernel 3 SMP runtime: %d tasks, 6 message buffers, 1 event flag, 1 cyclic handler\n",
           RT_NTASKS);
    return E_OK;
}
