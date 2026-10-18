// esp32s3_edge.ino
// ESP32-S3 firmware of the Configurable Edge Platform (D1). The shared code is
// the EdgeCore library; libraries/EdgeS3 supplies the HAL backend, the FreeRTOS
// runtime from the Week 6 design review and the Wi-Fi/MQTT layer. setup()
// starts the tasks and loop() is MonitorTask.
#include <EdgeS3.h>

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

void setup() {
    platform_console_begin(3000);
    printf("[BOOT] esp32s3_edge node %s\n", NODE_ID);
    runtime_set_network(WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT, NODE_ID);
    runtime_start();
}

void loop() {
    runtime_monitor_tick();
    delay(1000);
}
