#!/bin/sh
# check_mtk_image.sh [elf]: checks the linked µT-Kernel firmware itself, not
# the sources, for the change requirement's "the previous RTOS must not remain
# responsible" clause (D1 task 32). Run after a build, with ESP-IDF's
# environment loaded (sh tools/mtk.sh image does both).
#
# Fails if any part of FreeRTOS's kernel is linked: the scheduler start, the
# tick handler, the context switch, the ready-list and idle-task code, or
# ESP-IDF's FreeRTOS start-up path (main_task, esp_startup_start_app). Also
# fails if the course port's µT-Kernel start-up or dispatcher is missing. The
# FreeRTOS API functions that remain are the course shim's __wrap_ versions,
# which ESP-IDF's own libraries (Wi-Fi, lwIP, esp-mqtt) call and which run on
# µT-Kernel objects; the script counts them.
ELF=${1:-esp32s3_mtk/build/esp32s3_mtk.elf}
[ -f "$ELF" ] || { echo "FAIL  $ELF not found: build first (sh tools/mtk.sh build)"; exit 1; }
NM=xtensa-esp32s3-elf-nm
command -v $NM >/dev/null 2>&1 || NM=xtensa-esp-elf-nm
command -v $NM >/dev/null 2>&1 || { echo "FAIL  no xtensa nm: load ESP-IDF first (. ~/esp/esp-idf-v6.1/export.sh)"; exit 1; }

SYMS=$($NM "$ELF" | awk '{print $NF}')
fail=0
for s in vTaskStartScheduler xPortStartScheduler xTaskIncrementTick vTaskSwitchContext \
         prvIdleTask prvAddNewTaskToReadyList pxCurrentTCBs pxCurrentTCB xPortSysTickHandler \
         vPortSetupTimer xQueueGenericSend xTaskCreatePinnedToCore main_task esp_startup_start_app; do
  if echo "$SYMS" | grep -qx "$s"; then echo "FAIL  FreeRTOS kernel code linked: $s"; fail=1; fi
done
for s in __wrap_esp_startup_start_app knl_dispatch tk_cre_tsk tk_sta_tsk; do
  if ! echo "$SYMS" | grep -qx "$s"; then echo "FAIL  µT-Kernel symbol missing: $s"; fail=1; fi
done
n=$(echo "$SYMS" | grep -c '^__wrap_[xvu]')
[ $fail = 0 ] && echo "ok    $ELF: no FreeRTOS scheduler, tick, task or start-up code linked; µT-Kernel start-up and dispatcher present; $n FreeRTOS API functions served by the course shim on µT-Kernel"
exit $fail
