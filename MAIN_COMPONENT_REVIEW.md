# Main-branch component review and integration decision

Reviewed 3 November 2026 before changing the dev-jy-sensors workspace.

Upstream: `junyonggoh2004/EMBSYS-P2-AAI15`, main commit `49677c62c8738493f4e66667d1fe79afc947600d` (Merge pull request #6 from d3). Local dev-jy-sensors starts at `c42e5d2`, with a modified README and untracked ESP32 firmware/workspace files. The shared ancestor is `de4e40e`; this is a substantial framework evolution, not just a pin change.

## Component assessment

| Component | Main implementation / behaviour | Effect on this setup / decision |
|---|---|---|
| Shared framework | `libraries/EdgeCore/src`; also used by the Pico and older Arduino backends | Use these actual components rather than reproducing a private scheduler |
| Configuration | `application/config_parser.c`, `repl.c`, `bus_common.h`; BEGINCFG, defaults, poll/stream, inline/multiline commands | Preserve compatibility; add explicit optional I²C sensor profiles; no fixed pin assignments in portable drivers |
| JSON/MQTT configuration | `mqtt_json_parser.c` flattens dotted keys; `mqtt_config_parser.c` applies through shared parser | New I²C profile key naturally travels through the same parser; no parallel command system |
| Registry and lifecycle | `bus_if.c/h`; init/deinit/probe/poll, optional cancel; `BUS_POLL_PENDING` | Integrate sensor conversion waits through existing pending/cancel contract |
| Generic I²C | `bus/i2c.c`; setup pairs, optional repeated setup, register read | Suitable for register sensors; **not sufficient for AHT** because E1/BE/AC commands are full three-byte packets and conversion/status checking is needed. Keep raw behaviour as default, add device profiles |
| I²C backend | `hal_mtk.c`; two hardware controllers, open-drain 3V3 pins, 31-byte writes/32-byte reads, deferred repeated-start write paired with immediate read | Reuse on GPIO1/2; every profile register request completes its write+read within one scheduler call. Never retain a repeated-start transaction across conversion waits |
| AHT | No dedicated driver/profile in main; not a DHT one-wire device | Add AHT10/AHT20-compatible calibration/trigger/status state machine; preserve actual six-byte AHT frame; conversion sleeps become pending acquisition |
| TSL2561 | README has a raw generic I²C example, output is two raw channels | Add optional checked part ID, 101 ms integration, gain adjustment, saturation handling and lux conversion; retain generic example support |
| GPIO / ultrasonic | `bus/gpio.c`, `pulse_capture_mtk.h`; asynchronous edge capture using µT interrupt/event services | Reuse unchanged with TRIG GPIO4/ECHO GPIO5. A timeout is an error, never an invented distance |
| Timing and scheduling | `scheduler.c`, `timing_contract.c`; eight slots, periodic/one-shot/event, whole-acquisition deadline, 1 ms wake for pending work, STOP/CLEAR cancellation | Preserve ownership in SchedulerTask; profiles must not sleep through 80–120 ms conversion intervals. Use suitable explicit 600 ms deadlines in the preset |
| UART / radar | Shared UART driver, IRQ-fed 512-byte ring, `ld2450`, drop accounting | Preserve; no UART sensor is attached in this fixture |
| Pair/window | `pulse_sequence`, `entrance_window`, scheduler WINDOW/PAIR commands | Preserve; a second ultrasonic and LD2450 are absent, so three-sensor preset does not enable these features |
| Events / application | Schema-v2 EventRecord, canonical node/boot identity, deduplication/freshness, typed app_mbf, ApplicationTask owns occupancy | Preserve. Temperature/light readings must not be passed off as entrance events; synthetic APP commands remain diagnostics |
| Durable identity | `identity_mtk.c`; startup-only NVS boot counter, MAC identity, failure disables real window events | Preserve; no NVS erase workaround |
| Rules | `rules.c`, expression compilation/RPN, byte extraction, GPIO/PWM/log/network/inference actions | Preserve raw valid-frame publication and existing byte offsets; missing measurements must not become successful frames or trigger sample events |
| Output | `output_format.c`; console raw bytes → rules → timing event → MQTT raw and line topics | Preserve this contract; add supplementary physical-unit JSON for the configured profiles and pulse samples, with explicit null on failed acquisition |
| SPI | Shared validated SPI config plus low-level SPI2 backend and mutex | Preserve. The connected ST7789 has no driver here. Its SCK6/MOSI7/RST9/DC8/CS10 can remain connected while unused. Upstream SPI diagnostics use a different 11/12/13/10 fixture and must not be mistaken for an LCD test |
| External flash | `w25q32.c`, CRC-checked D2 log wire format, `spi_bench_mtk.c` | Preserve, but do not invoke with this fixture; no W25Q32 attached and its CS10 conflicts with retained LCD CS |
| Network | `net_mtk.c`, Wi-Fi/MQTT task, finite message buffers, retries and offline mode, bridging | Preserve; run offline without credentials. Upstream documents intermittent port Wi-Fi outages; no networking claim from local sensor build |
| Model store / inference | Two CRC-checked model slots in fixed flash partition; TFLite Micro 1.4.0, 64 KB PSRAM arena, request/reply to InferenceTask | Preserve. No model upload, flash erase or network traffic during integration testing |
| Runtime | Seven µT tasks plus two idle tasks, TA_FPU/fixed affinity, five message buffers, event flag, cyclic watchdog supervision, alarm pool | Keep main's complete runtime, including SchedulerTask on physical CPU1 and MQTTTask CPU0. Earlier sensor-only target used a different task/core arrangement and must not be mixed into this runtime |
| Console | Native USB LL backend, µT mutex, RX interrupt/ring, ControlTask command parsing | Preserve; Windows monitor should use main's bidirectional pyserial miniterm path, rather than assume IDF monitor passes typed commands |
| Memory | Quad 2 MB PSRAM for EdgeCore BSS and inference arena, kernel/shim noflash mapping, static internal stacks/queues | Preserve main's measured layout. Existing simple target did not need PSRAM; on-board PSRAM startup still needs verification on this physical board |
| Firmware / build | ESP-IDF commit 17a74c9, kernel e92874d, Xtensa 15.2.0; managed MQTT1.1.0/TFLM1.4.0; main provides shell tools | Reuse installed qualified SDK/kernel. Add Windows task support and an external kernel-path override while keeping the standard submodule default. Use a new build directory to avoid stale sensor-only objects |
| Other targets | Pico CMake/platform, Arduino EdgeS3, HAL test, SPI benchmarks, Wi-Fi reproduction | Review as reference/diagnostic backends; retain source, do not flash or wire their test fixtures automatically |
| Tests/evidence | Host timing/GPIO/pair/window/identity/event/application tests, target parity and D2 suites, historical logs | Run host baseline first, add sensor protocol/fault/cancellation tests, repeat relevant existing tests and full real firmware build. Historical hardware evidence is not a new local run |

## Decision

Use main's full configurable µT-Kernel framework as the active target, with optional `i2c.sensor=raw|aht|tsl2561` profiles. Keep raw I²C default behaviour and the existing GPIO pulse format. Add configuration and Windows commands for this exact three-sensor fixture. Keep LCD disabled and GPIO6–10 unused by the preset. Supplement valid raw frames with physical-unit JSON and emit null measurements for failed profile acquisitions.

Before modifying branch files, back up its modified/untracked source, documents, workspace and local build settings. Preserve the earlier sensor-only firmware separately. Import the reviewed main source snapshot into the working tree without committing or pushing; record upstream revision and every local adaptation. Keep the active workspace on dev-jy-sensors and avoid mixing duplicate main functions/build objects. Validate the candidate outside the active repository first.

## Verification boundaries

Compilation cannot prove wiring, acoustic range, PSRAM presence, SMP execution or Wi-Fi reliability. The user reports the three sensors worked in earlier hardware tests and the LCD did not. Do not describe historical results or main's committed logs as a successful run of the new local integration.
