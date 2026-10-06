#!/bin/sh
# Task 11 acceptance check: no shared module bypasses the HAL.
#
# Compiles every shared module with the host C compiler and only the project's
# own headers. Any call into the Pico SDK (or Arduino) fails here, because those
# headers are not on the include path.
#
# Platform code is deliberately not listed. It may use its SDK directly:
#   main.c                          Pico entry point (replaced by the task 13 runtime)
#   bus/gpio.c, bus/i2c.c, bus/uart.c   bus drivers behind bus_t (tasks 15-17)
#   mqtt/mqtt_telemetry.c           lwIP MQTT client (task 19)
#   mqtt/mqtt_model_transfer.c      flash writes; its transfer state machine moves
#                                   to shared code when task 20 adds the model store
#   inference_engine/tflite_wrapper.cpp   TFLite Micro port (task 20)
#   platform/pico/hal_pico.c        the Pico HAL backend
#
# Usage: sh tools/check_hal_boundary.sh   (from the repository root)

CC=${CC:-cc}
SRC=project_v3
SHARED="
application/repl.c
application/scheduler.c
application/config_parser.c
application/output_format.c
bus/bus_if.c
rule_engine/rules.c
rule_engine/rule_expr.c
rule_engine/rule_action.c
mqtt/mqtt_config_parser.c
mqtt/mqtt_json_parser.c
mqtt/sensor_bridge.c
inference_engine/inference_manager.c
"

fail=0
for f in $SHARED; do
    if $CC -fsyntax-only -std=c11 \
        -I$SRC/include -I$SRC/include/mqtt -I$SRC/include/third_party/jsmn \
        -DJSMN_PARENT_LINKS=0 -DJSMN_STRICT=0 "$SRC/$f"; then
        echo "ok    $f"
    else
        echo "FAIL  $f"
        fail=1
    fi
done
exit $fail
