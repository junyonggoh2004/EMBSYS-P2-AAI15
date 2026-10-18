// EdgeCore.h
// Entry header for the platform-independent core of the Configurable Edge
// Platform. An Arduino sketch includes it so that the IDE and arduino-cli find
// and compile this library; the Pico CMake build lists the same sources
// directly (CMakeLists.txt, EDGECORE).
#ifndef EDGECORE_H
#define EDGECORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "hal/hal.h"
#include "bus/bus_common.h"
#include "application/scheduler.h"
#include "rule_engine/rules.h"
#include "inference_engine/inference_manager.h"

#ifdef __cplusplus
}
#endif

#endif // EDGECORE_H
