// src/platform_esp32s3.h
// ESP32-S3 platform services that are not part of the shared HAL.
#ifndef PLATFORM_ESP32S3_H
#define PLATFORM_ESP32S3_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts the USB console and routes C stdout (printf) to it, which the shared
// code expects. Waits up to wait_ms for a host to open the port. Call once,
// first thing in setup(), before any other task is created.
void platform_console_begin(uint32_t wait_ms);

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_ESP32S3_H
