// main.c
// Entry point of the ESP32-S3 firmware on the course µT-Kernel 3 SMP port
// (D1 tasks 26-31). The port starts µT-Kernel instead of FreeRTOS and calls
// usermain() from its initial task; usermain creates the console objects and
// the runtime (runtime_mtk.c), then sleeps for good.
#include <stdio.h>
#include <tk/tkernel.h>
#include "esp_system.h"

#include "edge_mtk.h"
#include "runtime_mtk.h"

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

EXPORT INT usermain(void)
{
    ER er = console_init();
    static const char *const reasons[] = { "unknown", "power-on", "external", "software", "panic",
        "interrupt watchdog", "task watchdog", "other watchdog", "deep sleep", "brownout", "SDIO",
        "USB", "JTAG", "eFuse", "power glitch", "CPU lock-up" };
    esp_reset_reason_t why = esp_reset_reason();
    printf("[BOOT] esp32s3_mtk node %s: micro T-Kernel 3 SMP, %d processors, reset reason: %s\n", NODE_ID,
           (int)TK_MAX_CORE, (unsigned)why < sizeof reasons / sizeof reasons[0] ? reasons[why] : "other");
    runtime_set_network(WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT, NODE_ID);
    if (er == E_OK) er = runtime_start();
    if (er != E_OK) printf("[BOOT] start-up stopped: er=%d\n", (int)er);
    tk_slp_tsk(TMO_FEVR);   // usermain must not return: inittask treats it as shutdown
    return 0;
}
