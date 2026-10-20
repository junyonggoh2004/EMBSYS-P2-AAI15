# D2 — ESP32-S3 SPI Support To-Do List

- Branch: `s3-port-d2`
- Updated baseline: D1 `s3-port` at `ba81f18`, merged into D2 by `9a421ef`
- Team: Brendan and Jy
- Status: core SPI path verified; 22/22 loopback checks passed on COM5 with GPIO12-to-GPIO13 loopback and TFT disconnected. Five 8–1024-byte benchmark configurations (40,000 measured transfers total) and connected fault checks passed on 2026-10-17. A disconnected MISO still produced published raw bytes, so absent-device verification remains open. TFT display test remains unverified.

## End outcome

SPI is a first-class runtime-configurable interface behind the same `app_cfg_t`, `bus_t`, registry, scheduler, REPL, and publisher path used by GPIO, I2C, and UART. `proto=spi` must resolve through `bus_lookup("spi")`; shared application code must not call Arduino or ESP-IDF SPI APIs directly.

The ESP32-S3 implementation must support validated runtime host, SCLK, MOSI, optional MISO, CS, clock, mode, TX bytes, RX length, and timeout. Failed configuration, initialization, transfer, timeout, or verification must be explicit and must never be published as valid sensor data.

For the Week 6 system design, the real-device proof is a W25Q32 SPI flash on the processing node. `ApplicationTask` must record event data after publishing the occupancy result; a flash failure must be reported without interrupting live monitoring. The generic SPI transport remains device-independent, while W25Q32 commands and event-log policy belong in a device/application layer above it.

## Your next steps

Work in this order; the core SPI transport and loopback implementation are already complete.

1. [x] Run the dedicated SPI benchmark on COM5 with the TFT disconnected and GPIO12 MOSI jumpered to GPIO13 MISO. All 8,000 measured transfers passed at 10 MHz, mode 0; evidence is in `parity/logs/d2-spi/bench-20261017-140138/`.
2. [x] Exercise module-free faults: invalid/boundary clocks and transfer timeout on the loopback fixture; a separate HAL lock-contention test passed with the jumper removed. The disconnected-wire test exposed an open verification gap documented below.
   - [x] Extend the benchmark to modes 1 and 2 at 10 MHz and modes 0 and 3 at 20 MHz; all 32,000 additional transfers passed with zero HAL errors and zero byte mismatches. Production firmware was restored and the connected fault suite passed again.
3. [ ] Obtain or borrow a 3.3 V W25Q32 module; photograph its markings and the completed fixture before powering it. Until then, review the module-free [design checkpoint](D2_SPI_DESIGN_NOTES.md) with Jy.
   - [x] Draft the [event-record design template](D2_EVENT_RECORD_DESIGN.md): 32-byte logical format, dedicated typed queue (capacity 8, 0 ms enqueue, drop newest), publication ordering, failure diagnostics, and validation checklist. This is a provisional design, not firmware or hardware proof.
   - [ ] Review the template with Jy and freeze field mappings, queue ownership, CRC convention, event rate/retention, scratch address, erase/full-log policy, and T-23 thresholds. No erase is approved yet.
4. [ ] Add the W25Q32 device layer and prove device-ID/read/write operation through the existing generic SPI transport. Its JEDEC-ID/status/readback checks must reject absent/corrupt replies before any event is considered stored. Portable encode/decode and mocked fault tests can start without the module.
5. [ ] Add event logging in `ApplicationTask`: attempt to queue publication first, then enqueue a log request without blocking; write and read back from the consumer. Report a write failure and continue monitoring. Publisher enqueue is not broker delivery.
6. [ ] Run T-23 with SPI flash writes, live sensor sampling, and inference together. Record sampling jitter, SPI latency, mutex wait/hold time, queue use, stack high-water marks, heap, resets, and watchdog events.
7. [ ] Finish fault, regression, clean-build, evidence, teammate reproduction, and documentation gates before merge.

### Module-free checkpoint on 2026-10-17

- [x] Connected fault suite 6/6, disconnected-wire observation, HAL mutex contention, and 40,000 benchmark transfers across five configurations are retained in `parity/logs/d2-spi/`.
- [x] Production firmware was restored; SPI 22/22, D1 `4a` 14/14, and D1 `5-neg` 27/27 passed on COM5. Both S3 sketches compile, and the shared HAL-boundary check passes.
- [x] The [design note](D2_SPI_DESIGN_NOTES.md) and [event-record template](D2_EVENT_RECORD_DESIGN.md) record proposed absent-device checks, a versioned/CRC-protected logical record, publication ordering, a dedicated bounded queue/drop policy, and T-23 instrumentation. These are proposals, not implemented W25Q32/event-log functionality.
- [x] The `4b` GPIO and HAL-test fixture uses GPIO5↔GPIO6. The first attempt exposed a GPIO7↔GPIO6 wiring mistake; after moving GPIO7 to GPIO5, `4b` passed 25/25 and the corrected HAL sketch passed 23/23 with zero skips on 2026-10-17. Production firmware was restored afterward.
- [x] UART regressions with GPIO1↔GPIO2 on 2026-10-17: `4c-uart` 8/8, `t08` 26/26, and `t09` 5/5. Additional no-module stages passed: `t04` 13/13, `t03` 6/6, `t10` 7/7, and corrected `t06` 7/7. `4c-mag` remains unavailable without the GY-511 module.
- [ ] T-23 cannot be claimed from the current placeholders: `InferenceTask` and `ApplicationTask` in `runtime_s3.cpp` only drain queues. A real flash layer and application event pipeline are needed for the combined run.
- [ ] Team review, clean-clone reproduction, final module fixture, real-device and absent-device proof, and merge remain open. Existing unrelated Task 7 deletions and the optional TFT test directory have been left untouched.

## Professor feedback incorporated

- [x] Retain one shared SPI bus protected by `spi_mutex`; the implemented backend serializes transactions with a FreeRTOS mutex.
- [ ] Keep flash logging out of `SchedulerTask`; perform it through `ApplicationTask` so storage cannot directly block sensor acquisition.
- [ ] Define the capacity, timeout, overflow response, and drop policy for every queue used by logging or benchmarking.
- [ ] Measure p95 and maximum sampling/transfer latency in addition to means; D2 also records median and p99.
- [ ] Demonstrate model activity and SPI flash writes alongside live sampling, as prioritised in the professor's risk list.
- [ ] Replace generic board/placeholder artwork with a per-node wiring diagram that shows the actual ESP32-S3, W25Q32, power, ground, signals, and final GPIOs.
- [ ] Confirm that failed or delayed SPI logging never stops occupancy processing or causes a valid-looking reading.

## What D1 already completed

These are no longer D2 blockers or implementation tasks:

- [x] The shared sources are one Arduino library at `libraries/EdgeCore/src/`, also compiled by the Pico CMake build.
- [x] The ESP32-S3 platform layer is `libraries/EdgeS3/src/`; the production sketch is `esp32s3_edge/esp32s3_edge.ino`.
- [x] The registry and driver contract exist in `libraries/EdgeCore/src/bus/bus_if.h` and `bus_if.c`.
- [x] The scheduler owns bus calls from EngineTask; drivers do not mutate scheduler state from callbacks.
- [x] Invalid protocols are rejected through `app_cfg_t.cfg_error` instead of falling back to I2C.
- [x] The final D1 board, toolchain, USB, PSRAM, pin rules, test scripts, and evidence locations are documented.
- [x] D1 GPIO, I2C, UART, rules, MQTT, model storage/inference, fault, timing, and endurance tests have retained S3 evidence.
- [x] D2 requirements FR-16, FR-17, NFR-08 and tests T-17 to T-23 already exist in `parity/PARITY.md` as proposed rows.

Task 7 still requires every teammate to run the environment check, and D1 release task 23 remains pending, but neither blocks D2 implementation on this branch.

## Actual D1 integration points

| Concern | File(s) D2 must change |
|---|---|
| Protocol enum and runtime fields | `libraries/EdgeCore/src/bus/bus_common.h` |
| Parser and validation | `libraries/EdgeCore/src/application/config_parser.c` |
| Defaults, `SHOW`, and `CLEAR` | `libraries/EdgeCore/src/application/repl.c`, `scheduler.c` |
| Common driver contract and registry | `libraries/EdgeCore/src/bus/bus_if.h`, `bus_if.c` |
| Portable SPI sensor driver | new `libraries/EdgeCore/src/bus/spi.c` |
| Portable platform contract | `libraries/EdgeCore/src/hal/hal.h` |
| ESP32-S3 backend | `libraries/EdgeS3/src/hal_esp32s3.cpp` or a tightly scoped EdgeS3 SPI source |
| Pico compatibility | `project_v3/platform/pico/hal_pico.c`, root `CMakeLists.txt` |
| S3 build/test entry points | `tools/s3.sh`, new `parity/spi_test.py`, optional `esp32s3_spitest/` |
| Requirements and user documentation | `parity/PARITY.md`, `README.md`, `SETUP.md`, `ARCHITECTURE.md`, `project_v3/README.md` |

## Critical design corrections from the completed D1 code

- [x] Add an explicit `deinit(void *ctx)` callback to `bus_t`. `scheduler_reset()` previously called `free(ctx)` directly, which could not release an SPI device, shared bus reference, lock, or platform handle safely.
- [x] Update GPIO, I2C, and UART `bus_t` initializers with compatible `deinit` entries.
- [x] Make `scheduler_reset()` stop streaming, call `bus->deinit(ctx)` when present, and only use `free(ctx)` as the compatibility fallback.
- [x] On `init_cfg()` failure, clean up any partially returned context before releasing the scheduler slot.
- [x] Add `proto_spi` explicitly to `proto_name()` and return `NULL` for an invalid enum.
- [x] Keep the scheduler buffer at 128 bytes; reserve 8–1024-byte measurements for the dedicated benchmark path.
- [x] Keep configured TX bytes in a bounded 64-byte array inside `app_cfg_t`; no parser-buffer pointers are retained.
- [x] Keep device-specific commands out of the generic SPI transport. The required W25Q32 command/device layer remains to be implemented above it.

## Definition of done

- [x] `proto=spi` is parsed, validated, displayed by `SHOW`, scheduled, and resolved through `bus_lookup("spi")`.
- [x] Runtime configuration supports host, SCLK, MOSI, optional MISO, CS, clock, mode, bounded TX bytes, bounded scheduled RX length, and timeout.
- [x] Invalid settings are rejected before hardware state changes.
- [x] The generic EdgeCore driver uses only the HAL contract; Arduino/ESP-IDF headers remain below the platform boundary.
- [x] Compatible devices share one physical SPI host safely; incompatible host/pin reuse returns a clear conflict.
- [x] `CLEAR`, failed initialization, and 100 repeated reconfigurations release contexts and host/device references with no progressive heap loss.
- [x] TX-only and TX/RX paths have explicit success and typed error semantics.
- [x] Fault injection showed failed configuration leaves a running SPI device intact, an elapsed-transfer timeout is returned without publishing, and mutex contention times out without corrupting the in-progress transfer. `SPIClass` transfers themselves remain synchronous and cannot be aborted mid-transaction by the timeout.
- [x] GPIO12 MOSI-to-GPIO13 MISO loopback proves byte-for-byte RX integrity for 2-byte and 128-byte scheduled transfers in modes 0–3.
- [ ] A W25Q32 proves useful TX/RX operation through the same transport layer and stores a retrievable event record.
- [x] Payloads from 8 through 1024 bytes have retained latency, throughput, timeout/error, and mismatch results at 10 MHz, mode 0.
- [ ] D1 boundary, Pico build, S3 build, HAL, REPL, negative, sensor, inference, live-sampling concurrency, and clean-clone regressions pass.
- [ ] Evidence records commit, board, tool versions, pins, clock/mode, tester, and date.

## Confirmed hardware context

D1 final allocations are I2C GPIO1/2, HC-SR04 GPIO4/7, digital sensor GPIO8, PWM GPIO5, UART GPIO43/44, and native USB GPIO19/20. Strapping pins GPIO0/3/45/46 and flash/PSRAM pins are unavailable.

Use two separate D2 fixtures, with the final W25Q32 mapping approved before the real-device test:

1. **RX/integrity fixture:** FSPI/SPI2, SCLK GPIO11, MOSI GPIO12, MISO GPIO13, CS GPIO10; connect GPIO12 to GPIO13 for loopback.
2. **Required real-device fixture:** W25Q32 at 3.3 V. The Week 6 review proposes the same CS GPIO10, SCK GPIO11, MOSI GPIO12, and MISO GPIO13 mapping. Confirm the actual module labels and board pinout before wiring.

- [ ] Obtain a W25Q32 module and confirm its exact part marking and 3.3 V electrical requirements.
- [ ] Produce the processing-node wiring diagram using the actual board/module rather than the DevKitC-1 or placeholder artwork.
- [ ] Photograph and record each fixture before testing.

The existing SPI TFT is optional and is not the Week 6 D2 acceptance device. It cannot replace W25Q32 read/write, failure, and concurrent logging evidence.

## Teammate split

Keep one owner per active file to reduce conflicts.

### Brendan — shared contract, parser, scheduler, and integration

- [x] Add `proto_spi` and bounded SPI configuration fields to `app_cfg_t`.
- [x] Add parser defaults, parsing, validation, `SHOW`, and scheduler SPI summary.
- [x] Extend `bus_t` lifecycle semantics and update existing drivers without changing their behaviour.
- [x] Add `bus_spi` to the common registry and update Pico/S3 build manifests.
- [ ] Extend parity tests and keep README, architecture, configuration, and requirements synchronized.

### Jy — ESP32-S3 transport, fixtures, and evidence

- [x] Implement the EdgeS3 SPI host/device manager, locking, transactions, timeout/error translation, and cleanup.
- [x] Build and verify the GPIO12 MOSI-to-GPIO13 MISO loopback fixture.
- [ ] Build and verify the separate W25Q32 real-device fixture.
- [x] Implement and run the 8–1024-byte benchmark/test path using the same platform transaction code as `bus_spi`.
- [ ] Capture raw logs, heap measurements, build sizes, photos/video, and hardware metadata.

### Pair gates

- [x] Implement and locally verify the configuration schema, error values, scheduled transfer limit, and initial `SPIClass` backend choice.
- [ ] Review and approve the implemented `bus_t`, HAL, scheduler lifecycle, and C/C++ boundary together.
- [ ] Reproduce the main loopback test on the same commit before merge.

## Phase 1 — freeze the minimum contract

- [x] Use `fspi` as the canonical ESP32-S3 host name; accept `spi2` as a normalized alias.
- [x] First release supports poll mode only. Reject `mode=stream` for SPI with `unsupported` until queued streaming is deliberately implemented.
- [x] Define shared negative error codes for invalid configuration, unsupported operation, conflict/busy, timeout, I/O failure, verification failure, no memory, and absent device.
- [x] Define `poll_once()` semantics: positive RX byte count publishes data; zero is successful TX-only with no published sample; negative is an explicit error.
- [x] Set normal scheduled limits to `spi.tx_len <= 64` and `spi.read_len <= 128`.
- [x] Keep the 1024-byte benchmark outside normal publishing so console/MQTT line limits and EngineTask stack are not silently exceeded.
- [ ] Obtain team sign-off for FR-16, FR-17, NFR-08, and T-17 to T-23 before final merge.

Minimum configuration:

```ini
BEGINCFG|name=SPI_LOOP|proto=spi|mode=poll|freq_hz=2|spi.host=fspi|spi.sclk=11|spi.mosi=12|spi.miso=13|spi.cs=10|spi.hz=10000000|spi.mode=0|spi.tx=0xA5 0x5A|spi.read_len=2|spi.timeout_ms=100|ENDCFG
```

Validation checklist:

- [x] Host is supported and normalized (`fspi`, with `spi2` alias).
- [x] Mode is 0–3 and transfer bit order is fixed/documented as MSB-first in the HAL contract; mode validation and loopback checks passed.
- [ ] Clock is within an approved minimum/maximum; 1 kHz and 40 MHz 2-byte loopback boundary checks passed, but the team-approved maximum remains.
- [x] Pins pass `hal_gpio_valid()`, are not reserved, and are unique where required.
- [x] MISO is configured whenever RX bytes are requested.
- [x] CS is valid; no-CS operation is rejected.
- [x] Normal TX/RX lengths and timeout are bounded.
- [x] A device joining an existing host must match its SCLK/MOSI/MISO configuration.
- [x] Device-specific W25Q32/TFT fields do not enter the generic transport configuration.

## Phase 2 — shared EdgeCore changes

- [x] Add `proto_spi` without renumbering existing protocol values.
- [x] Add fixed-size SPI fields and defaults to `app_cfg_t` and `repl_init_defaults()`.
- [x] Parse all agreed `spi.*` keys and detect malformed, truncated, or excessive byte lists.
- [x] Perform complete SPI validation in `cfg_parse_end()` before `scheduler_add_config()` changes hardware.
- [x] Replace the scheduler's default-to-GPIO `proto_name()` logic with an exhaustive mapping that can return `NULL` for invalid values.
- [x] Add `deinit` to `bus_t`, update all existing drivers, and correct scheduler cleanup/rollback.
- [x] Add `bus_spi` to `bus_if.h`, `bus_if.c`, and new `bus/spi.c`.
- [x] Add a SPI-specific scheduler summary and make `SHOW` display all active SPI settings safely.
- [x] Add the new shared source to root `CMakeLists.txt`; the Pico backend supplies unsupported stubs.

## Phase 3 — HAL and ESP32-S3 backend

- [x] Define portable SPI open/transfer/close calls and typed errors in `libraries/EdgeCore/src/hal/hal.h`.
- [x] Add Pico stubs in `project_v3/platform/pico/hal_pico.c` that return `HAL_ERR_UNSUPPORTED`; D2 has no Pico hardware requirement.
- [x] Implement the S3 backend under EdgeS3 using Arduino `SPIClass` first.
- [x] Team agreed to retain the current `SPIClass` platform HAL implementation. Record the project's C/C++ boundary in the final report if requested by the professor.
- [x] Maintain one host record per bus/pin combination and one device record per CS/frequency/mode.
- [x] Serialize transactions with a FreeRTOS mutex.
- [x] Reject incompatible host reuse instead of silently reconfiguring an active bus.
- [x] Publish RX data only after a complete successful transfer.
- [x] Release device references and end the host after the final reference closes; the reusable mutex remains allocated once, with stable heap thereafter.
- [ ] Record DMA, alignment, maximum transfer, heap, and clock limitations. The current `SPIClass` path uses synchronous byte transfers without DMA; 128-byte scheduled and 1024-byte benchmark transfers passed, but the team-approved clock maximum and alignment statement remain open.

## Phase 4 — automated functional and fault tests

- [x] Add `parity/spi_test.py` and a `sh tools/s3.sh spi` entry point.
- [x] T-17: configure SPI through the REPL, confirm `CFG: ok`, `SHOW`, scheduler summary, and `bus_spi` execution.
- [x] T-18: validate host/pin/clock/mode settings, compatible bus sharing, and incompatible reuse refusal.
- [x] T-19: deterministic loopback payloads of 8, 16, 32, 64, 128, 256, 512, and 1024 bytes passed 1,000 transfers each through the existing SPI HAL in five configurations: modes 0/1/2 at 10 MHz and modes 0/3 at 20 MHz. Production firmware was restored after both benchmark sessions.
- [ ] T-20: identify, read, write, and verify a W25Q32 through the generic transport plus its device layer; demonstrate event-log retrieval.
- [ ] T-21: invalid host/mode, reserved/duplicate/conflicting pins, missing MISO, excessive RX, zero timeout, and empty transfers are rejected. Clock boundaries and forced transfer/mutex timeouts passed. With the jumper removed, all seven observed raw samples were still published with 63/64 bytes mismatched; generic SPI has no device-presence signal, so W25Q32-level reply/readback validation and absent-device rejection remain open.
- [x] T-22: 100 initialize/deinitialize cycles passed with heap 95,748 B before and after; failed incompatible reconfiguration left existing devices usable.
- [ ] T-23: run W25Q32 logging concurrently with live D1 sensor sampling and inference, with no reset, watchdog, missed sample, or unexplained corruption.
- [x] Add deliberate byte-for-byte mismatch detection to the loopback tests rather than treating received zeros as success.

## Phase 5 — benchmark and retained evidence

- [x] Run 10 warm-ups and 1,000 measured transactions per size at 10 MHz, mode 0.
- [x] Record median, p95, p99, maximum latency, and effective payload throughput.
- [x] Record attempts, platform errors, timeouts, mismatched transactions and bytes, observed resets, and heap before/after.
- [x] Report transaction error rate and byte error rate separately.
- [ ] Record SPI mutex wait/hold time, sampling jitter, queue high-water/overflow behaviour, and per-task stack high-water marks during the concurrent run.
- [ ] Approve and implement logging/benchmark queue capacities, timeout behaviour, overflow response, drop counters, and high-water diagnostics before the concurrent test. The provisional log-queue policy is documented in [D2_EVENT_RECORD_DESIGN.md](D2_EVENT_RECORD_DESIGN.md); it has not been tested.
- [x] Store raw serial output, 8,000 CSV latency samples, JSON metrics, and a concise result summary under `parity/logs/d2-spi/bench-20261017-140138/`.
- [ ] Record commit, ESP32-S3FH4R2/HW-747 board, Arduino CLI/IDE, core 3.3.12, ESP-IDF version, FQBN, CPU frequency, host, pins, mode, clock, backend, tester, and date.

## Phase 6 — exact D1 regression gate

- [x] Run `sh tools/check_hal_boundary.sh` (rerun through Git Bash on 2026-10-17).
- [x] Compile the bring-up and production sketches with the pinned environment. On 2026-10-17 the `tools/s3.sh check` wrapper could not find `arduino-cli` on Windows PATH, so the same two Arduino CLI compile commands were run directly and passed; this shell PATH issue is not a firmware failure.
- [x] Rebuild the Pico target from the shared sources; `bus_spi` compiled and `project_v3.elf` linked through unsupported Pico HAL stubs on 2026-10-16.
- [x] Run the HAL test sketch and `parity/hal_test.py`: 23/23, zero skips on 2026-10-17 after fixing one stale base-model expectation in the test sketch.
- [x] Run S3 stages `4a` and `5-neg`; rerun on restored D2 firmware on 2026-10-17 with 14/14 and 27/27, respectively.
- [ ] Run S3 stages `4b`, `4c-mag`, and `4c-uart`: `4b` passed 25/25 and `4c-uart` passed 8/8 on 2026-10-17; `4c-mag` still requires the GY-511 module.
- [x] Run the new `sh tools/s3.sh spi` suite: 22/22 on 2026-10-16.
- [ ] Run inference while SPI traffic is active and record task stack high-water marks and heap.
- [ ] Re-run MQTT/fault/endurance tests if scheduler buffers, publisher behaviour, task timing, or memory use changed.
- [ ] Compare new D1 results with retained logs under `parity/logs/s3/`; explain every difference.

## Phase 7 — documentation and merge gate

- [ ] Replace proposed D2 settings with the implemented schema and measured limits in all documentation.
- [ ] Fill the ESP32-S3 result/evidence columns for FR-16, FR-17, NFR-08, and T-17 to T-23.
- [ ] Update the final SPI pins without presenting the mutually exclusive loopback and W25Q32 fixtures as simultaneous wiring.
- [ ] Replace the generic DevKitC-1/placeholder diagram with a processing-node wiring diagram for the actual ESP32-S3 and W25Q32.
- [ ] Document that the TFT experiment is optional and does not satisfy the required W25Q32 event-log proof.
- [ ] List every direct ESP-IDF call and justify why Arduino SPI was insufficient, or record that no new exception was required.
- [ ] Review the final diff together and remove credentials, generated binaries, and local build output.
- [ ] Merge only when all Definition of done items pass and every evidence link resolves from a clean checkout.

## Remaining decisions before finalisation

- [x] Use jumper loopback as the initial RX/integrity proof; add W25Q32 as the required real-device proof.
- [ ] Confirm final W25Q32 wiring against its module markings and the approved SCK GPIO11/MOSI GPIO12 mapping.
- [x] Team agreed to keep the thin Arduino C++ `SPIClass` HAL adapter.
- [ ] Obtain team sign-off for normal scheduled TX/RX limits and the separate 1024-byte benchmark path.
- [ ] Approve maximum SPI clock and numerical performance thresholds.
- [ ] Confirm that queued/streaming SPI is deferred from D2.
- [ ] Reconcile ownership: the earlier D2 allocation names Brendan/Jy, while the Week 6 review assigns SPI drivers to Sitt and configuration/timing to Brendan.
