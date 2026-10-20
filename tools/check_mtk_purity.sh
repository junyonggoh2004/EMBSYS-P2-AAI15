#!/bin/sh
# D1 task 27 acceptance check for the µT-Kernel 3 SMP build: our code uses
# only µT-Kernel for tasks, scheduling, inter-task communication and
# synchronisation (the change requirement), and no Arduino or esp_driver_*
# component.
#
# Scans our µT-Kernel platform code (esp32s3_mtk/components, main, haltest, wifirepro)
# and the shared EdgeCore library. The course port's own components (its
# FreeRTOS-API shim for IDF's Wi-Fi, lwIP and esp-mqtt) live in
# esp32s3_mtk/third_party and are not ours, so they are not scanned.
#
# Usage: sh tools/check_mtk_purity.sh   (from the repository root)

DIRS="esp32s3_mtk/components esp32s3_mtk/main esp32s3_mtk/haltest/main esp32s3_mtk/wifirepro/main libraries/EdgeCore/src"

# FreeRTOS API (headers, objects, timing macros), Arduino, the IDF task
# watchdog, and IDF's interrupt-owning drivers.
PATTERN='freertos/|FreeRTOS\.h|\bxTask[A-Z]|\bvTask[A-Z]|\bxQueue[A-Z]|\bxSemaphore[A-Z]|\bxEventGroup[A-Z]|\bxTimer[A-Z]|\bxStream|\bxMessageBuffer|\bpdMS_TO_TICKS\b|\bportMAX_DELAY\b|\btaskYIELD\b|\bportENTER_CRITICAL|\bTaskHandle_t\b|\bQueueHandle_t\b|\bSemaphoreHandle_t\b|\bEventGroupHandle_t\b|\bTimerHandle_t\b|Arduino\.h|\bdelay\(|\bmillis\(|\besp_task_wdt|driver/(gpio|uart|i2c|i2c_master|ledc|spi_master|usb_serial_jtag|gptimer)\.h'

fail=0
files=$(find $DIRS -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' \) 2>/dev/null | sort)
for f in $files; do
    # Ignore comments: strip // to end of line and lines inside /* */ blocks.
    hits=$(sed -e 's://.*$::' "$f" | awk '
        /\/\*/ { inblk = 1 }
        { if (!inblk) print NR ": " $0 }
        /\*\// { inblk = 0 }' | grep -E "$PATTERN")
    if [ -n "$hits" ]; then
        echo "FAIL  $f"
        echo "$hits" | sed 's/^/        /'
        fail=1
    fi
done
n=$(echo "$files" | wc -l | tr -d ' ')
[ $fail -eq 0 ] && echo "ok    $n files: no FreeRTOS, Arduino, task-watchdog or esp_driver_* use"
exit $fail
