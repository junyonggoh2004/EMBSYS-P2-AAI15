# Validation and board test record

Date: 3 November 2026. Target: ESP32-S3 Super Mini, branch `dev-jy-sensors`.

## Build evidence

The target completed an ESP32-S3 compile and link with the pinned kernel and SDK, both in preparation and in the final repository folder. The exact Windows PowerShell command used by the VS Code task also passed an incremental build and the linked-image audit.

| Check | Result |
|---|---|
| Clean build in repository `esp32s3_mtk/build_mtk` | PASS, exit 0 |
| VS Code task command: Windows PowerShell 5.1, `tools/build.ps1 build` | PASS, exit 0 |
| Kernel source/configuration/linked-symbol audit | PASS |
| Image and bootloader partition fit | PASS |
| Application image | 173,728 bytes (`0x2a6a0`), 83% of 1 MiB app partition free |
| Bootloader | 21,024 bytes (`0x5220`) |
| App SHA-256 | `67a7a1e4fcb113f4577dd71c1328931c301c3b65b6bfefe3928aa0bb9a06771e` |
| Bootloader SHA-256 | `0a2908d8332310adc8b645caeb80b845afcba1a6bcdc66939883e42dbb302764` |

Hashes identify the images produced by this build; compilation timestamps and source revisions can change hashes in subsequent builds.

| Dependency | Tested revision |
|---|---|
| Public provisional micro T-Kernel port | `e92874d50d3a2968566f9628018f4c096c01f3d9` |
| ESP-IDF | `17a74c925caeaf43075e48ff5cbb8a5c7e506fa1` (`v6.1-dev-2300-g17a74c925c`) |
| Xtensa GCC | 15.2.0, Espressif `esp-15.2.0_20251204` |
| Python | 3.12.14, isolated IDF 6.1 environment |
| CMake / Ninja | 4.3.4 / 1.13.2 |

`tools/audit_build.py` checks the generated compiler inputs, configuration and linker map. It requires actual linked addresses for micro T-Kernel startup, dispatcher, second-core entry, timer handler, task creation, mutex operations, processor reporting and the native USB output wrappers. It rejects the original FreeRTOS task/queue/timer/event sources and Xtensa scheduler assembly, and linked FreeRTOS scheduler entry points. It also checks that application sources do not include Arduino, Wire or FreeRTOS headers.

The sensor C source passed an additional Xtensa `-std=c11 -Wall -Wextra -Werror -fsyntax-only` check. The full build produced no warnings in the four new application C files. The external kernel emits existing function-pointer/unused-variable warnings. The SDK may warn that optional ROM GDB helper generation is unavailable; this does not prevent compilation, linking or image generation. Debugging has not been validated.

These checks establish build configuration and linked-image contents. They do not prove that a scheduler has executed on a physical board.

## Isolation

The new workspace selects only `esp32s3_mtk`. The previous `esp32s3` directory, root Pico project, Arduino sketch and their build configurations are not imported. All 18 existing `esp32s3` files were checked by SHA-256, and the tracked Git diff was compared before and after adding this target. The original firmware sources remain unchanged. The branch README was subsequently updated, with the original Pico README preserved at `../docs/README-PICO-LEGACY.md`. No commit, push or device flash was performed by the migration work.

## User-reported hardware status

On 3 November 2026, the user confirmed that the AHT temperature/humidity, HC-SR04+ ultrasonic and Grove TSL2561 light sensors have worked. The ST7789 LCD remains non-functional (dim/white screen without text). Its Version 2 wiring can remain connected; the current ESP-IDF / µT-Kernel target leaves LCD support disabled. See [WIRING.md](WIRING.md).

These results describe the user's hardware testing. A captured on-board run of the migrated µT-Kernel firmware has not been recorded in this validation document.

## Board tests still required

No board has been flashed or measured as part of this migration. Perform wiring changes with USB disconnected. The ultrasonic wiring here assumes the previously identified 3.3 V-compatible HC-SR04+; a standard 5 V HC-SR04 is a different electrical case.

1. Open `AAI15-MTK.code-workspace` at the repository root and run **MTK: build**. Use the PowerShell commands in README.md to flash and monitor the actual COM port when ready. Close Arduino Serial Monitor before opening the same port in VS Code.
2. With sensor connectors disconnected, check that USB JSON continues updating, distance/temperature/humidity/light are `null`, and failure statuses are reported. `display_connected:null` and `display_enabled:false` are intentional.
3. With power off, connect AHT alone on GPIO1/2. Restore power and check sensible temperature/humidity plus `aht_status:"OK"`. Other missing sensors must remain `null`.
4. Repeat for the Grove TSL2561. Light readings should respond to covering/uncovering it; darkness can be a valid zero. Then check both I2C sensors together.
5. Repeat for HC-SR04+ using GPIO4 TRIG and GPIO5 ECHO. Aim at a flat target 20–50 cm away. Compare measurements against a ruler at several distances. A timeout also occurs with unsuitable targets and cannot by itself prove the module is disconnected.
6. Connect all three with power off, then run for at least ten minutes. JSON must report `sensor_processor:1` and `logger_processor:2`, monotonically increasing sequence numbers, no reset loop, and plausible readings. These are kernel processor numbers; physical CPUs are CPU0 and CPU1 respectively.
7. Disconnect/reconnect each sensor only with power off, then reboot and check missing-sensor and recovery behaviour. Additional runtime fault-injection, bus timing and CPU-load measurements are still needed before claiming timing guarantees.

The requirement names the course micro T-Kernel 3 SMP port without providing a source URL. The public port used for this build remains provisional until the lecturer confirms it is the approved course base. The original Pico application's networking, inference and rule engine are outside this sensor-only target's implemented scope.
