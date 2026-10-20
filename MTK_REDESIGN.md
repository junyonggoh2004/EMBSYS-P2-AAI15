# D1 Redesign: Port to µT-Kernel 3 SMP (Tasks 24–34)

Branch `s3-port-d1-redesign`, based on `s3-port-d2`. This document is the plan and the compliance record for the change requirement given at Tribunal 1 (19 Oct 2026). Each clause of the requirement maps to a design decision, a README task, and the evidence that closes it (section 9). D2 (SPI) and D3 onwards are outside this redesign; their owners will port them later on top of this runtime.

## 1. The change requirement, word for word

> Change Requirement: Port to µT-Kernel 3 SMP. The system firmware must be ported from its current RTOS to µT-Kernel 3 SMP, using the course µT-Kernel 3 SMP port. All existing application functionality must operate under µT-Kernel 3 SMP. The previous RTOS must not remain responsible for any application task, scheduling, inter-task communication or synchronisation. The requirement applies throughout the firmware, including: all application tasks and processing functions; sensor acquisition and processing; actuator and output control; communication and networking functions; inter-task communication; shared-resource protection and synchronisation; interrupt-to-task interaction; and system monitoring, control and other existing application functions. All application tasks must be implemented as µT-Kernel tasks. Existing RTOS queues, semaphores, mutexes, event mechanisms and other kernel objects must be replaced with appropriate µT-Kernel mechanisms. As this is an SMP implementation, teams must define and justify the task-to-core assignment. Data and resources shared between tasks or cores must be protected appropriately, and ISRs must interact safely with µT-Kernel tasks. The port must preserve the existing required functionality of the project.

## 2. What the course port is and what it changes for us

The course port is `sirfonzie/mtk3smp-esp32s3` (µT-Kernel 3.00 for the ESP32-S3, single-core and global-SMP profiles from one tree). It is an ESP-IDF project: it intercepts IDF's start-up with `--wrap=esp_startup_start_app`, starts one µT-Kernel dispatcher per core, and never starts the FreeRTOS scheduler. IDF components that were written against the FreeRTOS API (the Wi-Fi driver, lwIP, esp-mqtt) keep working through the port's `freertos_shim`, which implements that API on µT-Kernel tasks, semaphores, message buffers, cyclic and alarm handlers; IDF's own `freertos` component is shadowed with its scheduler removed. The port is qualified on ESP-IDF `v6.1-dev-2300-g17a74c925c` with `xtensa-esp-elf-gcc 15.2.0` (its `docs/PORT_ESP32S3.md`).

Three consequences follow for D1.

1. **Arduino-ESP32 cannot stay.** The Arduino core is built on IDF's FreeRTOS: `setup()`/`loop()`, `Serial`, `Wire`, `WiFi` and the MQTT library all run on FreeRTOS tasks and primitives. The redesign therefore replaces the whole Arduino platform layer (`esp32s3_edge.ino` and `libraries/EdgeS3`) with a native ESP-IDF project, `esp32s3_mtk/`, that links the course port. The Arduino build stays in the repository unchanged as the FreeRTOS baseline for before/after measurements and for the D2 work that still depends on it.
2. **The shared code does not change in kind.** `libraries/EdgeCore` (17 C files) contains no RTOS calls; `tools/check_hal_boundary.sh` proves it. It is compiled unchanged into the new build. Only the HAL backend and the runtime are rewritten, as planned at Tribunal 1.
3. **Peripheral drivers move to register level.** The port's rule (its `PORT_ESP32S3.md`) is that drivers are polled or interrupt-driven at register level through IDF's `hal/*_ll.h` and never use `esp_driver_*` components, because those components own interrupts and block on FreeRTOS primitives. The new HAL follows the same rule for GPIO, LEDC, I2C, UART and the USB console.

## 3. Mapping from the FreeRTOS design to µT-Kernel 3 SMP

| Week 6 / FreeRTOS mechanism | µT-Kernel 3 SMP replacement | Notes |
|---|---|---|
| `xTaskCreatePinnedToCore` (6 tasks) and the Arduino `loop()` MonitorTask | `tk_cre_tsk`/`tk_sta_tsk` with `TA_ASSPRC` and one `TP_PRCn` bit; MonitorTask becomes a real task | 7 application tasks, all pinned at creation (affinity is creation-time only in this port) |
| FreeRTOS priorities (higher number = higher priority) | `tk_pri = 25 − FreeRTOS priority` (lower number = higher priority) | The same formula the course shim uses for IDF tasks, so the order between our tasks and the Wi-Fi, lwIP and MQTT tasks is unchanged |
| Queues `sched_q` 6, `pub_q` 16, `infer_q` 4, `app_q` 8, `bench_q` 16 | Message buffers `tk_cre_mbf` of the same capacities; sends use `TMO_POL` (drop newest and log when full) | Message buffers copy the message, so no pointer to a stack buffer crosses a core |
| Event group `net_events` (Wi-Fi up, MQTT up) | Event flag `tk_cre_flg` with `TA_WMUL`; `tk_set_flg`/`tk_clr_flg`/`tk_ref_flg` | Set by MQTTTask and the network event handler |
| SPI/console mutexes (`xSemaphoreCreateMutex`) | `tk_cre_mtx` with `TA_INHERIT` and timed `tk_loc_mtx` | The console mutex makes each printed line atomic across both cores |
| One-shot FreeRTOS software timer ending a PULSE action | Alarm handler `tk_cre_alm`/`tk_sta_alm` | The handler only drives the pin low |
| Periodic wake-ups | Cyclic handler `tk_cre_cyc` waking MonitorTask with `tk_wup_tsk` | Monitoring runs on a kernel time base, not a sleep loop |
| `attachInterruptArg` GPIO ISR and Arduino UART driver task | `tk_def_int` on the GPIO and UART interrupt sources, registered from core 1 | Handlers do minimal work: count edges, timestamp echo edges, move UART bytes into a ring; they signal tasks with `tk_set_flg` |
| `esp_task_wdt` (task watchdog) | MonitorTask heartbeat supervision plus the RTC watchdog fed only when every supervised task is alive | IDF's task and interrupt watchdogs are unarmed in the port's SMP profile |
| `vTaskDelay`, `millis()`, `esp_timer_get_time()` | `tk_dly_tsk`; the 64-bit SYSTIMER count for microsecond timestamps | |
| Arduino `Serial` (USB CDC) | USB-Serial-JTAG at register level, writes under the console mutex | |
| Arduino `WiFi`, `MQTTClient` | `esp_wifi` and esp-mqtt owned by MQTTTask; inbound messages copied into `sched_mbf` | IDF's internal Wi-Fi, lwIP and esp-mqtt tasks run as µT-Kernel tasks through the course shim (section 6) |
| LittleFS model file | Two-slot model store in a raw flash partition with CRC32 and an atomic commit | No file-system locks; the model is used in place through the flash cache |

## 4. Task table and task-to-core assignment

| Task | Core (processor) | µT-Kernel priority | Week 6 FreeRTOS priority | Stack | Waits on |
|---|---|---:|---:|---:|---|
| SchedulerTask | 1 (`TP_PRC2`) | 21 | 4 | 16 KB | `sched_mbf` with a timeout equal to the next sensor due time |
| MQTTTask | 0 (`TP_PRC1`) | 22 | 3 | 8 KB | `pub_mbf` (10 ms) and the network state |
| ControlTask | 1 | 23 | 2 | 4 KB | USB console bytes (1 ms poll) |
| InferenceTask | 1 | 23 | 2 | 8 KB | `infer_mbf` |
| ApplicationTask | 1 | 23 | 2 | 4 KB | `app_mbf` (D6 application logic) |
| BenchmarkTask | 1 | 24 | 1 | 3 KB | `bench_mbf` |
| MonitorTask | 1 | 24 | 1 (Arduino loop) | 4 KB | wake-up from a 1 s cyclic handler |
| Idle (one per core) | 0 and 1 | 32 | – | 1 KB | never blocks; `waiti` |

**Why this assignment.** Core 0 carries the radio: the course shim pins every IDF task that may use the FPU (the Wi-Fi task, lwIP's tcpip thread, the event loop and esp-mqtt) to core 0, so MQTTTask, which drives those stacks, sits beside them and network bursts never pre-empt sampling. Core 1 carries everything time-critical and computational: SchedulerTask owns all configuration and sensor state and must meet the 50 ms observation window, so it runs at the highest application priority on the core with no radio work. The GPIO and UART interrupts are registered from SchedulerTask, so they are allocated on core 1 and wake it without a cross-core interrupt. InferenceTask shares core 1 at a lower priority so a model run can never delay a sample. Every application task is pinned (`TA_ASSPRC`, one processor bit) and created with `TA_FPU`, because the rule engine, inference and printing use floating point and the port rejects unpinned coprocessor tasks (`E_NOCOP`). Pinning everything also makes the assignment provable: `TASKS` prints each task's assigned processor and the processor it was last seen running on, and per-task residency counters show it never ran anywhere else (task 28).

## 5. Shared data and synchronisation

SchedulerTask is the single owner of configuration, sensor contexts, rules and inference state; every other task reaches that state only through `sched_mbf`. MQTTTask is the single owner of the Wi-Fi and MQTT clients; everything else publishes through `pub_mbf`. InferenceTask is the single owner of the TFLite Micro interpreter; it receives a complete, already-normalised input vector in each `infer_mbf` message and returns outputs and loss to SchedulerTask through `sched_mbf`, so no tensor or inference variable is touched by two tasks. Console output is serialised by a priority-inheritance mutex with a timeout, so a slow or absent host cannot block a task for longer than the timeout. Cross-core counters written by an ISR and read by a task (edge counts, ring indices) are single-writer words accessed with atomic loads and stores. Kernel objects are all created in `usermain` before any task starts, and every creation result is checked; a failure prints the object name and error code and stops start-up.

## 6. What still runs inside IDF, and why it is compliant

The requirement forbids the previous RTOS from being responsible for any application task, scheduling, inter-task communication or synchronisation. With the course port the FreeRTOS scheduler is never started: there is no FreeRTOS kernel in the image to be responsible for anything. Every task in the system, including the IDF service tasks, is a µT-Kernel task scheduled by the µT-Kernel dispatchers. IDF libraries written against the FreeRTOS API (Wi-Fi, lwIP, esp-mqtt, the C library's locks) reach µT-Kernel objects through the course shim; that is the mechanism the course port provides for vendor code, and it is used only below our code. Our own code (`esp32s3_mtk/` and EdgeCore) calls no FreeRTOS API at all, which `tools/check_mtk_purity.sh` enforces (task 27).

## 7. Decisions carried over from Tribunal 1

Hardware, wiring and the use case are unchanged; only firmware changes. EdgeCore is reused unchanged in kind; sensor-driver logic is reused and only RTOS-dependent operations are replaced. The five queues become message buffers with the same capacities and drop policy, and the event group becomes event flags. HC-SR04 capture stops busy-waiting: a GPIO interrupt timestamps the echo edges and the waiting task sleeps on an event flag (task 29). ISRs stay minimal and never touch application data; sensor interrupts are serviced on core 1. InferenceTask becomes a real task that owns the interpreter (task 31). The migration goes step by step, one subsystem per task, with tests after each step, and the same D1 tests are re-run unchanged so the FreeRTOS and µT-Kernel numbers compare like for like (task 32).

## 8. Memory plan

The first full build without PSRAM did not link: internal DRAM (341 KB, shared with IRAM) would have needed 281 KB of static `.bss` before Wi-Fi was even linked, because the shared code keeps large static tables (the rule table `g_rules` 68 KB, parser scratch `s_flat` 20 KB and `s_build` 17 KB) and the course port reserves a 44 KB kernel object heap and a 16 KB static rescue stack. The Arduino build survived the same tables only because it had PSRAM. The redesign therefore enables the module's 2 MB quad PSRAM and places only EdgeCore's static data there (`components/edgecore/linker.lf`, IDF's `extram_bss` scheme); nothing in EdgeCore runs in an interrupt handler or while the flash cache is off. Kernel objects, task stacks, message-buffer storage, the console and UART rings and everything an interrupt handler touches stay in internal RAM. Task stacks and message-buffer storage are static arrays passed with `TA_USERBUF`, so the kernel's object heap is left to the shim. With this layout about 199 KB of internal heap is free at run time (the Arduino build had about 48 KB). The TFLite Micro arena (64 KB) is allocated from PSRAM, as in the Arduino build, and the model runs in place from flash through the cache, so neither the built-in nor an uploaded model is copied into RAM. `TASKS` reports the free internal heap and every stack's headroom, and MonitorTask warns when a stack drops below 1 KB.

## 9. Compliance matrix

| Clause of the requirement | Design decision | Task | Evidence |
|---|---|---|---|
| Use the course µT-Kernel 3 SMP port | Course port as a pinned, unmodified submodule; qualified IDF and compiler versions | 25 | `tools/mtk.sh check`; boot banner |
| All existing functionality operates under µT-Kernel 3 SMP | Same EdgeCore, same commands, topics and output | 27–32 | Parity suite on the µT-Kernel build |
| Previous RTOS not responsible for tasks, scheduling, communication or synchronisation | FreeRTOS scheduler never starts; our code uses only µT-Kernel APIs | 26–28 | Boot log; `check_mtk_purity.sh` |
| All application tasks are µT-Kernel tasks | 7 tasks created with `tk_cre_tsk`, MonitorTask included | 28 | `TASKS` output |
| Queues, semaphores, mutexes, events and other objects replaced | Message buffers, event flags, mutexes, alarm and cyclic handlers (section 3) | 27–29 | Source; `check_mtk_purity.sh` |
| Define and justify task-to-core assignment | Section 4 | 28 | `TASKS` core column and residency counters |
| Shared data and resources protected | Single-owner tasks, message buffers, console mutex, atomic ISR words (section 5) | 27–29 | Concurrency and flood tests |
| ISRs interact safely with µT-Kernel tasks | `tk_def_int` handlers on core 1 that only count, timestamp or buffer and then `tk_set_flg` | 29 | Pulse, counter and UART tests |
| Sensor acquisition and processing | GPIO, pulse, counter, one-wire, I2C, UART through the µT-Kernel HAL | 27, 29 | 4a, 4b, 4c, 5-neg, t-rows |
| Actuator and output control | GPIO write/toggle, PULSE by alarm handler, PWM by LEDC | 27 | 4b |
| Communication and networking | MQTTTask with esp_wifi and esp-mqtt | 30 | MQTT, fault and flood tests |
| Inter-task communication | Five message buffers and one event flag | 28 | Flood test; `TASKS` drop counters |
| System monitoring and control | MonitorTask (stacks, heap, heartbeats, RTC watchdog), ControlTask, BenchmarkTask | 28 | `TASKS`; endurance run |
| Preserve the required functionality | Re-run every D1 test unchanged and compare with FreeRTOS | 32 | PARITY.md section 4 |

## 10. Test plan and hardware needs

The same scripts in `parity/` run against the µT-Kernel build, because it answers on the same USB port with the same output format. Tests that need no wiring (console, configuration, rules, inference, invalid input, timing, cycle time, MQTT, model upload, faults, flooding, endurance, the digital light sensor) run as each task lands. Tests that need jumpers or sensors (the GPIO loopback 5↔6 and 1↔2, the UART loopback, the 20,000-edge counter, the GY-511 on I2C and a real HC-SR04 through its divider) are marked "hardware pending" until the board is rewired.
