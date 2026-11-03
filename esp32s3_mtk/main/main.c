// main.c
// Entry point of the ESP32-S3 firmware on the course µT-Kernel 3 SMP port
// (D1 tasks 26-31). The kernel initial task has only a 1 KiB stack.
// usermain only launches a larger startup task; all application/SDK startup
// runs there, including printf, NVS identity and runtime object creation.
#include <stdio.h>
#ifdef EDGE_STARTUP_TEST
#include "startup_test.h"
#else
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "esp_system.h"

#include "edge_mtk.h"
#include "runtime_mtk.h"
#endif
int identity_mtk_init(void);

#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#endif
#ifndef WIFI_SSID   // no credentials file: run offline
#define WIFI_SSID ""
#define WIFI_PASS ""
#define MQTT_HOST ""
#define MQTT_PORT 1883
#define NODE_ID   "s3-001"
#endif

#define STARTUP_STACK_BYTES (16u * 1024u)

static void app_start_task(INT code,void *context)
{
    (void)code; (void)context;
    ER er = console_init();
    static const char *const reasons[] = { "unknown", "power-on", "external", "software", "panic",
        "interrupt watchdog", "task watchdog", "other watchdog", "deep sleep", "brownout", "SDIO",
        "USB", "JTAG", "eFuse", "power glitch", "CPU lock-up" };
    esp_reset_reason_t why = esp_reset_reason();
    printf("[BOOT] esp32s3_mtk node %s: micro T-Kernel 3 SMP, %d processors, reset reason: %s\n", NODE_ID,
           (int)TK_MAX_CORE, (unsigned)why < sizeof reasons / sizeof reasons[0] ? reasons[why] : "other");
    printf("[BOOT] application startup stack: %u bytes, kernel processor %d\n",
           (unsigned)STARTUP_STACK_BYTES,(int)tk_get_prc());
    runtime_set_network(WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT, NODE_ID);
    if (er == E_OK && identity_mtk_init() != 0)
        puts("[BOOT] durable event identity unavailable: WINDOW disabled; NVS was not erased");
    if (er == E_OK) er = runtime_start();
    if (er != E_OK) printf("[BOOT] start-up stopped: er=%d\n", (int)er);
    tk_exd_tsk(); // deletes this task and releases its kernel-allocated stack
}

EXPORT INT usermain(void)
{
    T_CTSK task={0};
    task.task=(FP)app_start_task;
    task.tskatr=TA_HLNG|TA_RNG0|TA_ASSPRC|TA_FPU;
    task.assprc=TP_PRC1; // physical CPU0; floating-point tasks must be pinned
    task.itskpri=5;
    task.stksz=STARTUP_STACK_BYTES; // kernel allocates it; no permanent BSS stack
    ID id=tk_cre_tsk(&task);
    ER er=id<0 ? id : tk_sta_tsk(id,0);
    if (er!=E_OK) {
        if (id>=0) tk_del_tsk(id);
        tm_printf((UB*)"[BOOT] startup task creation/start failed: %d\n",(int)er);
    }
    // Returning from usermain shuts the kernel down. Keep this tiny initial
    // task asleep, even if it is accidentally awakened later.
    for (;;) tk_slp_tsk(TMO_FEVR);
}
