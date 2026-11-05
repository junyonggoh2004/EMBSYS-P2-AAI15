# Main-based sensor integration validation

Date: 3 November 2026. Branch: `dev-jy-sensors`. Reviewed main snapshot: `49677c62c8738493f4e66667d1fe79afc947600d`.

## Completed checks

### Board startup finding and correction

The first user-provided startup capture confirms a successful flash, USB console output and a passing 2 MB PSRAM memory test, followed by `STACK OVERFLOW: task id=4 ... HALT` immediately after the application BOOT message. The port's initial task has a 1 KiB stack (`include/sys/inittask.h`); the original application entry performed console formatting, network settings, NVS identity initialization and runtime creation on that stack.

The corrected entry only creates/starts a pinned, FPU-enabled µT-Kernel startup task with a 16 KiB kernel-allocated stack. All application initialization runs there. The startup task deletes itself when initialization completes, releasing its temporary stack; the kernel's small initial task remains asleep. The kernel source and stack guards are unchanged. Native tests of the actual entry cover separation from the small stack, ordering, worker cleanup, initialization failure and task-creation/start failure. A subsequent user-provided startup capture reached the seven-task runtime, loaded the built-in model and continued scheduling. The later sensor capture below confirms readings on the corrected firmware; the overflow belongs to the preceding image.

Serial monitor tasks now display a startup banner and local echo so typed input is visible. Local echo is not acknowledgement from the board. Use the ESP-IDF diagnostic monitor when capturing reset/boot output, as a USB reset can invalidate miniterm's connection.

| Check | Result |
|---|---|
| Full candidate ESP32-S3 compile/link | PASS |
| Clean build from the final repository, using Windows PowerShell 5.1 exactly as the VS Code task does | PASS, exit 0 |
| Actual ELF / source-input audit | PASS: complete configurable framework, sensor profiles and µT-Kernel SMP linked; original FreeRTOS scheduler excluded; native USB and PSRAM enabled |
| Firmware partition fit | PASS: corrected image 1,288,896 bytes (`0x13aac0`), 58% of the `0x2f0000` application partition free |
| Startup-stack correction | PASS: native startup lifecycle tests, incremental firmware rebuild and linked-kernel audit; board startup and three-sensor readout subsequently observed |
| AHT / TSL2561 driver and decoder tests | PASS: complete command packets, calibration fallback, pending conversion waits, deadline, wrong ID, saturation, darkness, NACK/short reads, cancellation/recovery, raw-I²C compatibility |
| Real shared scheduler + real I²C driver with simulated hardware | PASS: all three sources interleave; failed AHT does not stop peers or publish valid rule frames; STOP/CLEAR cancel pending work; retries recover |
| Existing D3 host regression suite | PASS: timing/contracts, GPIO lifecycle, actual pulse-capture implementation with register seams, pair/window engine, identity, UART drain, event safety, application dispatch, scheduler timing, rule provenance, actual runtime task bodies |
| Existing application smoke-runner unit tests | PASS: 4 tests |
| Sensor preset loader unit tests | PASS: 6 tests, including readiness, exact acknowledgements, refused configuration and timeout handling |
| External W25Q32 / log host suite | PASS: faults, codec, CRC, recovery, torn/full log |
| HAL boundary | PASS: shared modules compile without SDK headers |
| µT-Kernel API-purity check | PASS: 74 active files |

Tests with simulated hardware exercise the actual shared scheduler and I²C protocol code. They do not establish physical sensor readings or target timing guarantees. The external flash tests do not access a real W25Q32.

## Sources and build adaptations

The framework comes from the reviewed main commit. SDK and kernel pins match main: ESP-IDF `17a74c925caeaf43075e48ff5cbb8a5c7e506fa1`, kernel `e92874d50d3a2968566f9628018f4c096c01f3d9`, Xtensa toolchain `esp-15.2.0_20251204`. Managed MQTT and TFLite Micro dependencies retain main's lock.

Local adaptations are optional `i2c.sensor=aht|tsl2561` profiles, physical-unit console JSON/null reporting, the current pin preset, Windows build/monitor/configuration helpers and VS Code tasks. Generic raw I²C and valid raw-frame/rule/MQTT publication remain available. The retained Pico build's explicit source list includes the two new portable modules, but a Pico build or Pico profile timing was not validated; this integration targets the ESP32-S3 µT-Kernel runtime.

Main's scheduler fixture needed explicit test-only GPIO capture fallback stubs for the Windows PE/COFF linker. The target HAL is unchanged. Windows build issues were resolved with Ninja response files, the application's SDK dependency closure, and a shorter output path. The final target uses main's normal `build/` directory, separate from old `build_mtk/` results. Existing kernel/SDK warnings and the unavailable optional ROM-GDB helper do not prevent the successful firmware build; debugger operation is not validated.

Protocol checks also used the primary [Adafruit AHT implementation](https://github.com/adafruit/Adafruit_AHTX0/blob/master/Adafruit_AHTX0.cpp) and [TSL2561 implementation](https://github.com/adafruit/Adafruit_TSL2561/blob/master/Adafruit_TSL2561_U.cpp). The AHT profile reads the six-byte AHT10-compatible frame and does not validate an AHT20 CRC byte.

## Preservation and source import

Before importing main, 160 source/document/settings/image files and the original working-tree diff were backed up under the local `Documents/ChatGPT/ES/main_integration_20261103/backup_before_import` folder. Earlier firmware sources and prior images are also preserved in `legacy/esp32s3_sensor_bringup/` and `legacy/esp32s3_idf_initial/`, with file hashes verified. Windows held the old folder open, so preservation used verified copies and removed only obsolete archived source/config files; the original generated `build_mtk/` remains ignored and unused.

The main source snapshot and submodule reference were imported into the existing branch's index/worktree; HEAD remains unchanged. Local adaptations are additional working-tree changes. No commit, push, flash, model upload or hardware-test command was performed. Historical evidence from main remains historical; it is not this board's new test result.

## Remaining board checks

The user reports that AHT, HC-SR04+ and Grove TSL2561 worked in earlier hardware tests. The LCD remains dim/white without text. The corrected integration has now produced readings from all three sensors on the physical board, as documented below.

1. Keep [WIRING.md](WIRING.md): SDA1/SCL2, TRIG4/ECHO5, shared 3V3/GND. This assumes the identified 3.3 V-compatible HC-SR04+. LCD wiring can remain; its pins 6–10 are unused by the preset.
2. Build, flash, then run `tools/build.ps1 sensors -Port COMx` to load the three configurations and RUN. The loader replaces volatile sensor/rule configuration via STOP/CLEAR. Load it again after a reset.
3. Use `tools/build.ps1 monitor -Port COMx`; verify PSRAM startup, HELP, SHOW and TASKS. The full framework requires main's 4 MB flash / 2 MB quad PSRAM layout. SchedulerTask belongs to physical CPU1 and MQTTTask CPU0.
4. Check plausible readings, cover/uncover the light sensor, and measure a flat target at several distances. A timeout cannot distinguish a missing ultrasonic module from an unsuitable target.
5. With USB disconnected between wiring changes, test each missing sensor and recovery. The failed configured sensor should print null/status while peers continue. Darkness may correctly report zero lux.
6. Run all three for at least ten minutes; check resets, stack headroom, core residency, queue drops and service timing. Keep the LCD disabled. Do not run upstream SPI/flash/loopback fixtures with the retained LCD connected.

The current physical-unit output is per-sensor JSON alongside main's raw lines, rather than the earlier combined bring-up JSON object. Networking, inference, window and flash components are retained; their operation has not been revalidated on this board.


## Observed board runtime — 3 November 2026

After uploading the startup-stack correction, the user supplied a boot capture showing the 16 KiB startup task, the seven-task µT-Kernel SMP runtime, successful built-in model loading and a live scheduler. The sensor loader then acknowledged all three configurations and RUN. A subsequent [runtime screenshot](../docs/evidence/sensors-runtime-20261103.png) shows concurrent readings:

| Sensor | Observed readings | Status |
|---|---|---|
| HC-SR04+ | 161.6–164.5 cm, echo 9423–9590 µs | OK, error 0 |
| AHT | 22.01–22.04 °C, 71.62–71.63% RH | OK, error 0 |
| Grove TSL2561 | 20 lux | OK, error 0 |

The capture shows `running=1`, increasing timestamps and approximately 2 s between samples of each sensor. This confirms startup and multi-sensor acquisition for the supplied capture. It does not establish calibrated accuracy, a ten-minute endurance run, physical fault/recovery testing, networking or inference correctness. TASKS/core-residency evidence remains to be collected. LCD remains disabled and non-functional; existing wiring is retained.
