#!/bin/sh
# Task 11/12 acceptance check: no shared module bypasses the HAL.
#
# Every source in the shared EdgeCore library is compiled with the host C
# compiler and only the library's own headers. Any call into the Pico SDK or
# Arduino fails here, because those headers are not on the include path.
#
# Platform code lives outside the library and may use its SDK directly:
#   project_v3/          Pico 2 W: main.c, lwIP MQTT client, flash model
#                        transfer, TFLite Micro wrapper, platform/pico/hal_pico.c
#   libraries/EdgeS3/    ESP32-S3: HAL backend, FreeRTOS runtime, and stubs for
#                        the services tasks 19-20 port
#
# Usage: sh tools/check_hal_boundary.sh   (from the repository root)

CC=${CC:-cc}
LIB=libraries/EdgeCore/src

fail=0
for f in $(cd $LIB && find . -name '*.c' | sort); do
    if $CC -fsyntax-only -std=c11 -I$LIB "$LIB/$f"; then
        echo "ok    ${f#./}"
    else
        echo "FAIL  ${f#./}"
        fail=1
    fi
done
exit $fail
