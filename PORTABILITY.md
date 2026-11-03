# D1 Portability and Design-Review Closure (Task 22)

This document records how the Pico 2 W reference (`pico-reference`) maps onto the ESP32-S3 port, every exception to the Arduino-only rule, the behaviour that differs between the boards, and how the implementation compares with the submitted Week 6 design review. Sections 1 to 5 describe the Arduino/FreeRTOS build (tasks 11–22), which is now the measured baseline; test evidence is in `parity/logs/s3/` and PARITY.md §3. Since Tribunal 1 the D1 runtime is µT-Kernel 3 SMP on the course port; section 6 maps the same dependencies onto that build (tasks 24–34), and [MTK_REDESIGN.md](MTK_REDESIGN.md) holds its design and compliance matrix.

## 1. Pico dependencies and their replacements

Shared code is the EdgeCore library (`libraries/EdgeCore/src/`), compiled unchanged by both builds; `tools/check_hal_boundary.sh` compiles every file in it without SDK headers (NFR13). Platform code is `project_v3/` (Pico) and `libraries/EdgeS3/src/` (ESP32-S3).

| Pico dependency (reference location) | Replacement | ESP32-S3 implementation |
|---|---|---|
| `pico/time.h` timing in the scheduler, drivers, rules and inference manager | `hal_time_us`, `hal_time_ms`, `hal_delay_ms`, `hal_delay_us` | `esp_timer_get_time`, `delay`, `delayMicroseconds` |
| `getchar_timeout_us` and USB stdio in `repl.c` | `hal_console_getc`; C `printf` routed by the backend | `Serial` (USB CDC) and a `fopencookie` stdout stream |
| `hardware/gpio.h` in `rule_action.c` and `bus/gpio.c` | `hal_gpio_valid`, `hal_gpio_mode`, `hal_gpio_write`, `hal_gpio_read`, `hal_gpio_toggle` | `pinMode`, `digitalWrite`, `digitalRead` |
| Blocking `sleep_ms` inside the PULSE action | `hal_gpio_pulse` | FreeRTOS one-shot software timer (Pico keeps the blocking pulse) |
| Level sampling in counter mode | `hal_gpio_count_edges`, `hal_gpio_edge_count` | `attachInterruptArg` IRAM ISR (Pico: GPIO IRQ callback) |
| `hardware/pwm.h` | `hal_pwm_set`, `hal_pwm_stop` | LEDC (`ledcAttach`, `ledcWrite`), 37.463 kHz, 10-bit |
| `hardware/i2c.h` in `bus/i2c.c` | `hal_i2c_open`, `hal_i2c_write`, `hal_i2c_read` | `Wire`, `Wire1` |
| `hardware/uart.h` in `bus/uart.c` | `hal_uart_open`, `hal_uart_write`, `hal_uart_read_byte` | `Serial1`, `Serial2` (`HardwareSerial`) |
| `main.c` super-loop with a 1000 ms sleep | FreeRTOS runtime | `runtime_s3.cpp`, task table in §4 |
| `cyw43_arch` Wi-Fi and lwIP MQTT (`mqtt_telemetry.c`) | `mqtt_telemetry.h` interface, same topics and payloads | `mqtt_s3.cpp`: Arduino `WiFi` and the MQTT library by Joel Gaehwiler 2.5.3 in MQTTTask |
| `pico-tflmicro` (`tflite_wrapper.cpp`) | `tflite_wrapper.h` interface | `tflite_wrapper_s3.cpp` on the TFLite Micro bundled with the Arduino-ESP32 core (task 20) |
| `hardware/flash.h` model transfer (`mqtt_model_transfer.c`) | `mqtt_model_transfer.h` interface | `model_transfer_s3.cpp`: Arduino `LittleFS` on the Huge APP data partition; the model is written to a temporary file and replaces the saved one only after the size and CRC32 check, then is read into PSRAM at boot |

## 2. ESP-IDF exceptions

Each sits below the HAL boundary and is exercised by a recorded test.

| Exception | Why Arduino is not enough | Test |
|---|---|---|
| `esp_timer_get_time` | Arduino `micros()` is 32-bit and wraps after about 71 minutes; the core uses 64-bit time | `hal_test.py`: time monotonic, ms matches µs, delays, tick versus host clock |
| `esp_task_wdt` (SchedulerTask, ControlTask) | Arduino has no per-task watchdog | Whole suite: no watchdog reset or panic in any S3 log |
| `fopencookie` stdout to `Serial` | Arduino sends `printf` to UART0, not the USB console the shared code prints to | Every stage reads `printf` output over USB |
| `wifi_err_reason_t` codes in Wi-Fi events | Arduino reports a disconnect but not a readable cause | Used to diagnose the hotspot ("network not found") during task 19 |
| TFLite Micro from the core's ESP-IDF components | No Arduino Library Manager build matched the reference operators (task 20) | `parity/task20/`: 40 invocations, `MSE=0.4179` |

FreeRTOS calls (tasks, queues, event groups, software timers) are the RTOS the Week 6 design specifies and are part of the Arduino-ESP32 core.

## 3. Differences between the boards

**Timing.** The Pico samples at most once per second because of its fixed loop delay (D-06). The S3 SchedulerTask waits exactly until the next sensor is due and meets NFR1 (PARITY.md §3). **Concurrency.** The Pico runs one loop; the S3 runs the Week 6 task set, with SchedulerTask the only writer of configuration, sensor and rule state, so the shared tables need no locks. Interrupts only increment per-pin edge counters (32-bit, atomic). **Memory.** The S3 firmware is about 1.29 MB, which needs the *Huge APP (3MB No OTA/1MB SPIFFS)* partition scheme on the 4 MB flash. The static rule, sensor and parser tables take about 177 KB of internal RAM, so the 64 KB tensor arena and large buffers are allocated from the board's 2 MB PSRAM (PSRAM *QSPI*); with the arena in internal RAM Wi-Fi could not start. After the full suite 53.7 KB of internal heap and 2.0 MB of PSRAM are free and every task keeps more than 2 KB of stack free. Driver contexts were never freed on `CLEAR` in the reference and now are. A batch rule action used to copy the 17 KB `rule_t` onto the stack, which overflowed silently on the Pico's 2 KB main stack and reset the S3 (D-14, fixed). **Hardware.** S3 GPIO is 3.3 V only, so the HC-SR04 Echo needs a 1:2 divider; GPIO19–20 (USB console), GPIO26–32 (flash and PSRAM), GPIO22–25 (absent) are refused by `hal_gpio_valid`; strapping pins 0, 3, 45 and 46 are avoided. The UART needs the receive buffer cleared after set-up, or a stray byte puts every reply out of step (D-05, fixed). **Unsupported or pending.** One-wire is untested because the kit has no DHT sensor; SPI is D2; the unquoted-JSON form of `cfg.to` is still rejected by the receiver.

## 4. Reference defects

| Defect | ESP32-S3 and shared code |
|---|---|
| D-01 unknown protocol accepted as I2C | Fixed: refused, `CFG: add failed (-5)` |
| D-02 missing `gpio.pin` reads GP2 | Fixed: refused |
| D-03 malformed `when=` accepted | Fixed: syntax check, `RULE: parse error` |
| D-04 bad action pin found only when fired | Fixed: refused when the rule is added |
| D-05 UART receive buffer never flushed | Fixed: flushed after set-up |
| D-06 sampling capped at 1 Hz | Fixed on the S3 by the RTOS scheduler; the Pico keeps its loop |
| D-07 out-of-range echo reads as valid | Unchanged (frame format kept for parity); on the bench GPIO7 reads high with no sensor, so t06 reports the 25 ms timeout as a valid width |
| D-08 nonexistent pin accepted | Fixed: refused |
| D-09 boot reports Wi-Fi failure, then connects | Fixed on the S3: state printed on every change |
| D-10 bridging drops every remote line | Fixed on the S3: verified over MQTT |
| D-11 `cfg.to` forwards `\|` escapes | Fixed: escapes removed before sending |
| D-12 `uptime_s` counts heartbeats | Fixed on the S3: seconds |
| D-13 one Pico freeze after 28 minutes idle | Pico only; the S3 continuous run is in PARITY.md §3 |
| D-14 batch action copies 17 KB onto the stack | Fixed |
| D-15 CPU1 interrupt watchdog during S3 start-up (about 1 boot in 6–15) | Open on the S3; the board reboots itself and runs normally; Wi-Fi modem sleep and flash writes are off as a mitigation |

## 5. Design consistency with the Week 6 review

The implemented task names, priorities, cores, queues (`sched_q`, `pub_q`, `infer_q`, `app_q`, `bench_q`), the `net_events` group, the GPIO edge ISR and the one-shot PULSE timer match review section 3. Two items differ and need the review updated: the review's hardware section names three ESP32-S3-WROOM-1-N16R8 DevKitC-1 boards with the console on GPIO43/44, while every test ran on the HW-747 Super Mini (ESP32-S3, 4 MB flash, 2 MB PSRAM) with the console on native USB; and `i2c_mutex`/`spi_mutex` are not needed yet, because SchedulerTask is the only bus user until ApplicationTask (D6) drives the LCD and flash. Queue capacities and overflow behaviour, an open gap in review section 8.3, are now defined: `sched_q` 6 × 2.2 KB, `pub_q` 16 × 680 B, `bench_q` 16; a full queue drops the new item and logs it, and the flood test (T-13) shows the node keeps running. MQTT bridging and reconnection, another 8.3 gap, are verified in `parity/logs/s3/mqtt.log` and `net-faults.log`.

## 6. The µT-Kernel 3 SMP build (tasks 24–34)

The EdgeCore library and its HAL interface are unchanged; `esp32s3_mtk/` replaces the Arduino platform layer. Following the course port's rule, peripherals are driven at register level through ESP-IDF's `hal/*_ll.h` and never through `esp_driver_*` components, and no code of ours calls a FreeRTOS or Arduino API (`tools/check_mtk_purity.sh`).

| Pico dependency | Arduino/FreeRTOS build | µT-Kernel 3 SMP build |
|---|---|---|
| `pico/time.h` timing | `esp_timer_get_time`, `delay` | SYSTIMER count through `esp_timer_get_time`; `tk_dly_tsk` for delays |
| USB stdio and `getchar_timeout_us` | `Serial` and a `fopencookie` stream | USB-Serial-JTAG registers; `printf` wrapped at link time and serialised by a µT-Kernel mutex; receive interrupt into a ring, task woken by an event flag |
| `hardware/gpio.h` | `pinMode`, `digitalWrite`, `digitalRead` | `gpio_ll` registers |
| Blocking PULSE | FreeRTOS one-shot software timer | µT-Kernel alarm handler |
| Counter mode and echo timing | `attachInterruptArg` ISR; echo busy-wait | GPIO interrupt registered with `tk_def_int` on core 1: counts edges and timestamps echo edges; the measuring task sleeps on an event flag (task 29) |
| `hardware/pwm.h` | LEDC through `ledcWrite` | `ledc_ll` registers, same 10-bit resolution |
| `hardware/i2c.h` | `Wire`, `Wire1` | `i2c_ll` command lists, polled, with timeouts; repeated start kept |
| `hardware/uart.h` | `Serial1`, `Serial2` | `uart_ll` registers; receive interrupt into a ring |
| `main.c` super-loop | FreeRTOS tasks and queues | µT-Kernel tasks pinned at creation, message buffers, event flag, cyclic and alarm handlers (MTK_REDESIGN.md §3–§5) |
| `cyw43_arch` Wi-Fi and lwIP MQTT | Arduino `WiFi`, MQTT library 2.5.3 | `esp_wifi` and esp-mqtt 1.1.0 owned by MQTTTask; IDF's network tasks run as µT-Kernel tasks through the course shim |
| `pico-tflmicro` | TFLite Micro bundled with the core | `espressif/esp-tflite-micro` 1.4.0 (the same release), owned by InferenceTask |
| `hardware/flash.h` model storage | LittleFS file, read into PSRAM | Two slots in a raw `model` partition with a CRC-checked header written last; the model runs in place through the flash cache |
| Task watchdog | `esp_task_wdt` | MonitorTask heartbeats and timer group 0's hardware watchdog |

ESP-IDF is still used below the HAL for start-up, the flash and PSRAM drivers, the Wi-Fi driver, lwIP, esp-mqtt and TFLite Micro; that is how the course port is built, and section 6 of MTK_REDESIGN.md explains why it leaves µT-Kernel solely responsible for scheduling, communication and synchronisation. Results on this build are in PARITY.md §4.
