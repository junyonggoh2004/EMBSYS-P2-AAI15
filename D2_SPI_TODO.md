# D2 — ESP32-S3 SPI Support To-Do List

- Branch: `s3-port-d2`
- Updated baseline: D1 `s3-port` at `ba81f18`, merged into D2 by `9a421ef`; D1 µT-Kernel redesign `main` at `1f41dac` fast-forwarded into D2 on 25 October 2026. The D2 checkpoints through `f535a45` were pushed to `origin/s3-port-d2` on 26 October 2026.
- Team: Brendan and Jy
- Status (25 October 2026): the **µT-Kernel** SPI HAL passed 22/22 REPL/configuration/loopback/lifecycle checks, 6/6 connected fault checks, and five 8–1024-byte benchmark configurations (40,000 measured transfers, zero HAL errors or byte mismatches) on COM5. The physical W25QXX breakout returned JEDEC `EF4016`, passed the W25Q32 device-layer probe/read, and its initially blank final 4 KiB sector was approved as disposable scratch. A single 16-byte page-program at `0x3FF000` passed blank check, status polling, readback, ESP-reset and full USB power-cycle retention without erase. With all flash wires removed, the device layer returned `HAL_ERR_ABSENT (-9)`. The earlier Arduino/FreeRTOS results are retained as comparison baselines. Event logging, D3/D6 schema agreement, concurrent T-23 performance, and final integration remain open.

## Handoff status — 26 October 2026

- **Ready for other D tasks:** the shared SPI configuration/driver/HAL path and the physical W25Q32 device layer are available on `s3-port-d2`. The ESP32-S3 on COM5 has the µT-Kernel firmware with the D2 flash test commands. A synthetic rejected-event record passed program, immediate readback, cold power-cycle CRC recovery and preservation of the older scratch pattern. These are flash-path proofs, not occupancy or live-event results.
- **Current fixture:** ESP32-S3 connected to the W25Q32 using 3V3, GND and GPIO10/11/12/13; no loopback jumper. The first 128-byte log slot at `0x3FF100` contains an 80-byte synthetic test record. The 16-byte pattern at `0x3FF000` remains. Do not repeat either program command or erase the sector without a new plan.
- **D2 still open:** agree D3/D6 event identity and result mapping, connect real processed events to `ApplicationTask` with bounded µT-Kernel logging, run T-23 with live sensors/inference, close remaining target faults/regressions and obtain teammate sign-off. The D2 record layout is provisional; downstream code must not treat it as a frozen application contract.
- **Scope for the next team:** D3/D4 and other tasks can proceed with their own work. This checkpoint does not claim final D2 acceptance or change their application, inference or sensor implementations. The only team-owned source touched for this flash fixture is the additive SPI command dispatch in `runtime_mtk.c`.

## D2 checkpoint — 25 October 2026

- **Current fixture:** ESP32-S3 on COM5 with GPIO12↔GPIO13 loopback; external flash disconnected. Remove the jumper before reconnecting the flash. The approved final-sector scratch pattern remains on the external module; do not run `SPIPROGRAM` again or erase it without a new plan.
- **Completed on the migrated build:** SPI transport/configuration/fault/benchmark suites and physical W25Q32 ID, read, one-time scratch program/readback, reboot retention and absent-device rejection. Evidence is linked below. These results are real target measurements, but they do not prove event logging.
- **Decision recorded:** Brendan wants a log entry for every processed event, including rejected, duplicate and degraded outcomes. Freeze the byte format only after D3/D6 agree the event identity and application result contract. The earlier 32-byte draft is retired.
- **Next D2 gates at this 25 October checkpoint:** agreed D3/D6 interface; versioned record codec and recovery; bounded µT-Kernel log handoff in `ApplicationTask`; remaining target SPI fault checks; T-23 live sampling/inference/flash run; final D2 regression, documentation and teammate review. This historical checkpoint did not claim D2 completion; the 26 October update below supersedes its codec and push status.

### 26 October D2-only implementation update

- [x] Add an 80-byte provisional event codec, CRC-32 golden vector, 128-byte non-erasing slots, reboot scan, torn-slot skip, readback verification and explicit full-area result in D2-owned flash files. The host mock passes applied/no-change/duplicate/reordered/rejected records and program-failure recovery. Pico and µT-Kernel builds pass. See [the format and limits](D2_EVENT_RECORD_DESIGN.md#on-flash-format-awaiting-d3d6-agreement).
- [x] Push the D2-only implementation and the previous hardware/benchmark checkpoint to `origin/s3-port-d2` at `f535a45`; the push was fast-forward and did not include unrelated local Task 7 deletions or Tribunal draft artifacts.
- [ ] Obtain D3/D6 approval of node/boot identity, result and reason mappings, then integrate a bounded log handoff into their µT-Kernel `ApplicationTask`. The current D3 branch is unmerged and the team-owned runtime has no event-log call site. The later flash-only fixture added console dispatch in `runtime_mtk.c`; it did not modify D3/D6 event processing or add an application log call site.
- [x] With a new explicit hardware-write plan and approval, test one synthetic, rejected D2 log record in a fresh slot at `0x3FF100`. The 80-byte write, immediate readback, cold power-cycle recovery and preservation of the earlier 16-byte pattern passed on 26 October. This proves the physical codec/log allocator path, not real D3/D6 event integration. Do not repeat either program command.
- [ ] Run T-23 and final integration/regression evidence after the application handoff exists. The 30-slot, no-erase scratch layout is a proof fixture; event rate, retention, full/wrap/erase policy and production capacity remain open.

### 26 October flash-only test preparation (ESP32-S3 + W25Q32)

- [x] Reconnected the six-wire flash fixture on COM5 and ran read-only checks: JEDEC `EF4016`, repeated address-zero read, retained 16-byte pattern at `0x3FF000`, and a full final-sector scan. The scan found exactly 16 non-`FF` bytes, starting at `0x3FF000`; the verified 16-byte pattern accounts for all of them, so the proposed `0x3FF100` log area is blank. [Retention log](parity/logs/mtk/d2-spi/flash-reconnect-verify-20261026.log). No flash write or erase occurred.
- [x] Prepare a **synthetic, explicitly rejected** 80-byte D2 fixture with test-only identity and reason marker. Add `SPILOGPLAN` (read-only), `SPILOGWRITE EF4016 3FF100` (one guarded write), and `SPILOGVERIFY` (read-only recovery) to the existing D2 benchmark-command path. Only the D2 flash test source and the existing SPI command dispatch in the µT-Kernel runtime changed; D3/D6 application logic did not change. The D2 host mock and both changed target translation units compile.
- [x] Build and flash the complete µT-Kernel image, then run `SPILOGPLAN` and compare its printed 80 bytes against the independently computed CRC vector. Windows CMake required a forward-slash `IDF_PATH` override after `export.ps1`; the full image then built and was uploaded to the ESP32-S3 on COM5. [Read-only log-plan evidence](parity/logs/mtk/d2-spi/flash-log-plan-20261026.log) reports `BLANK slots=30 valid=0 invalid=0`, the expected CRC `DCCC25B4`, and no external-flash write. The independent checker is `parity/spi_flash_log_plan.py`.
- [x] Show the exact `0x3FF100` 80-byte fixture and obtain approval for this **new** external-flash program. Brendan approved the one-time synthetic-record write on 26 October after reviewing its effects. `SPILOGPLAN` confirmed the area blank; `SPILOGWRITE EF4016 3FF100` ran exactly once, programmed 80 bytes without erase, and passed immediate readback (`elapsed_us=4600`). A separate read-only `SPILOGVERIFY` found the exact test-only record with valid CRC. [Raw write evidence](parity/logs/mtk/d2-spi/flash-log-write-once-20261026.log). The earlier 16-byte pattern at `0x3FF000` was untouched.
- [x] After unplugging/reconnecting ESP32-S3 USB with all flash wires retained, read-only `SPILOGVERIFY` recovered the synthetic record with a valid CRC. The earlier scratch pattern at `0x3FF000` also still passed read-only `SPIVERIFY`. Evidence: [cold log recovery](parity/logs/mtk/d2-spi/flash-log-cold-verify-20261026.log) and [original pattern preservation](parity/logs/mtk/d2-spi/flash-old-pattern-post-log-20261026.log). Do not rerun the write command: the occupied first log slot is intentionally one-time.

## End outcome

SPI is a first-class runtime-configurable interface behind the same `app_cfg_t`, `bus_t`, registry, scheduler, REPL, and publisher path used by GPIO, I2C, and UART. `proto=spi` must resolve through `bus_lookup("spi")`; shared application code must not call Arduino or ESP-IDF SPI APIs directly.

The ESP32-S3 implementation must support validated runtime host, SCLK, MOSI, optional MISO, CS, clock, mode, TX bytes, RX length, and timeout. Failed configuration, initialization, transfer, timeout, or verification must be explicit and must never be published as valid sensor data.

For the Week 6 system design, the real-device proof is a W25Q32 SPI flash on the processing node. `ApplicationTask` must record event data after publishing the occupancy result; a flash failure must be reported without interrupting live monitoring. The generic SPI transport remains device-independent, while W25Q32 commands and event-log policy belong in a device/application layer above it.

Under [Tribunal 1 Change Requirement 3](TRIBUNAL_1_CHANGE_REQUIREMENT_3_CONTEXT.md), the final firmware must also use µT-Kernel 3 SMP for application tasks, scheduling, communication and synchronization. D2 cannot retain an application FreeRTOS SPI mutex or log queue. The existing C driver/HAL contract and logical record design may be reused if the actual course port supports the required SPI, timing and synchronization behavior; verify that support rather than assuming Arduino `SPIClass` or a specific µT-Kernel API is available. The exact runtime migration is team-wide, while D2 owns proving its SPI and event-log behavior on the migrated build.

## 25 October branch-sync checkpoint and revised priorities

On 25 October, `git fetch --all --prune` refreshed every remote branch. Local `main` and `s3-port-d2` were fast-forwarded to `origin/main` (`1f41dac`); the other pre-existing local branches already matched their upstreams. New remote-only branches `origin/s3-port-d1-redesign` and `origin/s3-port-d5` were available without creating local checkouts. `origin/d3` is a separate, unmerged application/EventRecord branch based on the earlier D2 line; do not merge it blindly across the µT-Kernel redesign. Existing unrelated Task 7 deletions were retained. The later 26 October D2 push is recorded above.

### Without the W25Q32 module — do now

- [x] **Validate the ported SPI HAL on the board.** The register-level SPI2/FSPI backend and µT-Kernel mutex built and passed physical flash and GPIO12↔GPIO13 loopback checks; no Arduino SPI dependency is used by this backend. See the target evidence below.
- [x] Rerun configuration/negative, loopback, connected faults, 100 open/clear cycles and 8–1024-byte benchmarks on µT-Kernel. [SPI suite: 22/22](parity/logs/mtk/d2-spi/spi-20261025.log), [fault suite: 6/6](parity/logs/mtk/d2-spi/fault-connected-20261025.log), and five [benchmark reports](#25-october-target-benchmark-evidence) passed. The clock/mode matrix matches the old FreeRTOS baseline; the new logs are separately labelled µT-Kernel.
- [ ] Agree the D2/D3/D6 application interface before freezing flash bytes. `origin/d3` currently defines an 80-byte `event_record_t` with `event_id`, `source_id`, `source_seq`, window timestamps, classes and quality flags. The [integration review](D2_EVENT_RECORD_DESIGN.md#25-october-integration-review--decision-needed-from-d3d6) maps those to proposed flash fields; the earlier 32-byte draft is retired. Brendan chose to log every processed event. Agree a new explicit layout after D3/D6 settle reboot identity, result enums and persistence semantics.
- [x] Add provisional portable record encode/decode, CRC golden vector and torn-record/recovery tests. The device-independent W25Q32 command layer and host mock tests also cover probe, absent/wrong ID, read/program/readback, scratch/page bounds, timeout, stuck busy and corruption (`sh tools/d2_host_test.sh`: PASS). **Field semantics are not frozen** and the physical tests prove the device layer, not an event record. Queue capacity, drop-newest accounting, publisher-enqueue ordering, retention policy and numerical T-23 thresholds still need owner approval.
- [ ] Coordinate integration ownership: D1's µT-Kernel `ApplicationTask` currently only drains `app_mbf`, while `origin/d3` implements application logic on the older Arduino runtime. Plan the merged µT-Kernel event path and a separate bounded log handoff without putting flash work in `SchedulerTask`.

**Before the physical tests on 25 October:** `tools/d2_host_test.sh` passed on this Windows host. The pinned course-port submodule and ESP-IDF `17a74c925c` were installed locally; `esp32s3_mtk` built and linked successfully after normalizing Windows `IDF_PATH` slashes in the build environment. The source purity check passed (49 files); the linked-image check passed with the µT-Kernel dispatcher present and no FreeRTOS scheduler linked. Subsequent physical results are recorded below. Those results were saved in checkpoint `2b095c4` and pushed with the 26 October D2 update.

### 25 October target benchmark evidence

With the ESP32-S3 GPIO12↔GPIO13 loopback and TFT/flash disconnected, the µT-Kernel `BenchmarkTask` completed 1,000 measured transfers per payload size (8, 16, 32, 64, 128, 256, 512, 1024 bytes), after 10 warm-ups, in each configuration below. All 40,000 measured transfers passed with zero HAL errors and zero mismatched bytes. Raw serial, 8,000-row CSV and JSON are inside each report directory.

| Clock | Mode | 8-byte median | 1024-byte median | Report |
|---:|---:|---:|---:|---|
| 10 MHz | 0 | 36 µs | 1045 µs | [results](parity/logs/mtk/d2-spi/bench-20261025-213501/RESULTS.md) |
| 10 MHz | 1 | 35 µs | 1044 µs | [results](parity/logs/mtk/d2-spi/bench-20261025-213529/RESULTS.md) |
| 10 MHz | 2 | 35 µs | 1044 µs | [results](parity/logs/mtk/d2-spi/bench-20261025-213551/RESULTS.md) |
| 20 MHz | 0 | 32 µs | 631 µs | [results](parity/logs/mtk/d2-spi/bench-20261025-213610/RESULTS.md) |
| 20 MHz | 3 | 33 µs | 632 µs | [results](parity/logs/mtk/d2-spi/bench-20261025-213635/RESULTS.md) |

For the same 10 MHz/mode-0 loopback payloads, the earlier Arduino `SPIClass` baseline measured 35 µs at 8 bytes and 2281 µs at 1024 bytes; the µT-Kernel register-level backend measured 36 µs and 1045 µs. These are transport-only measurements, not flash-write latency or live-sampling jitter. Three µT-Kernel capture attempts failed before receiving a benchmark header because Windows serial writes stalled; they are not performance results. A full USB power cycle restored writes, the capture was given a bounded write timeout, and all five complete runs then passed.

The target's free internal heap was 92,716 bytes before and after each complete benchmark run. Failed capture folders were removed after diagnosing the serial-write issue; the five complete reports and their raw logs were retained.

### With the W25Q32 module — hardware acceptance

- [x] Confirm the six-wire 3.3 V fixture and physical read-only response. Photos show a W25QXX breakout with `VCC, CS, DO, GND, SLK, DI`; the chip's top marking is still not legible. Brendan manually traced `VCC→3V3`, `CS→GPIO10`, `DO→GPIO13` (MISO), `GND→GND`, `SLK→GPIO11`, `DI→GPIO12` (MOSI), after removing the loopback. On COM5 the µT-Kernel console returned JEDEC `EF4016` and the W25Q32 layer's repeat-read check passed. [Raw evidence](parity/logs/mtk/d2-spi/flash-readonly-20261025.log) was captured by `parity/spi_flash_readonly.py`. Do not connect the GY-511 or HLK-LD2450 as the flash fixture.
- [ ] Finish physical T-20/T-21: the user-approved scratch program/readback, ESP-reset retention, full USB power-cycle retention and absent-device rejection now pass. Actual event-log retrieval and the remaining target fault cases still need proof. Never erase unknown contents or report a transfer alone as a stored event.
- [x] Read-only scan showed `0x3FF000–0x3FFFFF` initially held 4,096 `FF` bytes. Brendan approved it as disposable D2 scratch on 25 October. Exactly 16 bytes at `0x3FF000` were programmed once and verified; no erase was issued. Do not rerun the program script on this now nonblank sector. Evidence: [raw serial log](parity/logs/mtk/d2-spi/flash-program-once-20261025.log), `parity/spi_flash_program_once.py`.
- [x] After unplugging USB for about five seconds and reconnecting, `SPIVERIFY EF4016` read the same 16-byte pattern from external flash. Evidence: [cold power-cycle log](parity/logs/mtk/d2-spi/flash-cold-verify-20261025.log), `parity/spi_flash_verify.py`. The first capture parser missed a valid response because a startup line was concatenated with it; the raw log exposed this, the parser was corrected, and the read-only capture passed on retry. No second program was attempted.
- [x] With the flash completely disconnected and no loopback jumper, `SPICHECK EF4016` returned `probe=-9` (`HAL_ERR_ABSENT`), with `read1=-9`, `read2=-9`, and `close=0`. Evidence: [absent-device log](parity/logs/mtk/d2-spi/flash-absent-20261025.log), `parity/spi_flash_absent.py`. No valid flash result was reported.
- [ ] Run actual event logging with live sampling and inference on µT-Kernel (T-23). Measure flash-write and SPI-lock latency, sampling p95/p99/max and skips, log-buffer high water and drops, heap/stack, reset/watchdog outcomes; demonstrate that flash failure does not stop monitoring or alter occupancy. Retain source revision, board/part and wiring photos.
- [ ] Finish D1/D2 regressions, parity/evidence documentation, teammate reproduction and a clean build before merging or pushing a D2 completion claim.

## Your next steps

Steps 1–2 below preserve the pre-change baseline. The 25 October µT-Kernel rerun is documented [above](#25-october-target-benchmark-evidence) and in the physical flash evidence. The remaining execution path is the D3/D6 event contract, integrated log queue/codec, concurrent T-23 run and final review. Do not use old-firmware hardware results as µT-Kernel evidence.

1. [x] Run the dedicated SPI benchmark on COM5 with the TFT disconnected and GPIO12 MOSI jumpered to GPIO13 MISO. All 8,000 measured transfers passed at 10 MHz, mode 0; evidence is in `parity/logs/d2-spi/bench-20261017-140138/`.
2. [x] Exercise module-free faults: invalid/boundary clocks and transfer timeout on the loopback fixture; a separate HAL lock-contention test passed with the jumper removed. The disconnected-wire test exposed an open verification gap documented below.
   - [x] Extend the benchmark to modes 1 and 2 at 10 MHz and modes 0 and 3 at 20 MHz; all 32,000 additional transfers passed with zero HAL errors and zero byte mismatches. Production firmware was restored and the connected fault suite passed again.
3. [x] Obtain the 3.3 V W25QXX breakout and photograph both sides and the completed ESP fixture. The printed chip marking remains too dark to read, but the physical JEDEC ID is `EF4016`; the [read-only](parity/logs/mtk/d2-spi/flash-readonly-20261025.log), [one-time program](parity/logs/mtk/d2-spi/flash-program-once-20261025.log), [cold retention](parity/logs/mtk/d2-spi/flash-cold-verify-20261025.log) and [absent-device](parity/logs/mtk/d2-spi/flash-absent-20261025.log) logs are retained. Review the [design checkpoint](D2_SPI_DESIGN_NOTES.md) with Jy.
   - [x] Draft the [event-record design template](D2_EVENT_RECORD_DESIGN.md), recording the retired 32-byte draft, every-processed-event scope, dedicated typed queue proposal (capacity 8, 0 ms enqueue, drop newest), publication ordering, failure diagnostics, and validation checklist. The provisional codec has host proof and a physical synthetic-record write/recovery proof; real application event logging remains open.
   - [ ] Review the template with Jy and freeze field mappings, queue ownership, CRC convention, event rate/retention, scratch address, erase/full-log policy, and T-23 thresholds. No erase is approved yet.
   - [ ] Coordinate with D6 on the canonical sensor-node event ID (node ID, boot ID, sequence), observation-window ownership, and occupancy-after-reset decision (feedback F9/F10). Confirm that the D2 flash record can carry or unambiguously reference that ID before freezing its format. D6 owns event formation, de-duplication, and occupancy persistence; D2 owns durable SPI logging of the agreed event.
4. [x] Add the W25Q32 device layer and prove JEDEC-ID/read/one-time program/readback/retention through the generic SPI transport **on the migrated µT-Kernel build**. With the flash removed, it returned `HAL_ERR_ABSENT`; host mocks also cover wrong ID, timeout, stuck busy, partial program and bad readback. This proves device-level storage, not an event-log record or concurrent application behavior.
5. [ ] Add event logging in the migrated `ApplicationTask`: attempt to queue publication first, then offer a log request through a bounded µT-Kernel-compatible mechanism without blocking sampling; write and read back from the consumer. Report a write failure and continue monitoring. Publisher enqueue is not broker delivery.
6. [ ] Run D2 T-23 on µT-Kernel with SPI flash writes, live sensor sampling, and inference together. Record sampling jitter, SPI/flash-write latency, lock wait/hold time, queue use, stack high-water marks, heap, core/task evidence, resets, and watchdog events. Compare with the pre-change baseline; do not reuse its pass verdict. Coordinate a separate D4 F11/R4 model-upload-during-sampling test; a model upload pass alone does not complete D2 T-23, and D2 T-23 does not prove the D4 upload path.
7. [ ] Finish fault, regression, clean-build, evidence, teammate reproduction, and documentation gates before merge.

### Module-free checkpoint on 2026-10-17

- [x] Connected fault suite 6/6, disconnected-wire observation, HAL mutex contention, and 40,000 benchmark transfers across five configurations are retained in `parity/logs/d2-spi/`.
- [x] Production firmware was restored; SPI 22/22, D1 `4a` 14/14, and D1 `5-neg` 27/27 passed on COM5. Both S3 sketches compile, and the shared HAL-boundary check passes.
- [x] The [design note](D2_SPI_DESIGN_NOTES.md) and [event-record template](D2_EVENT_RECORD_DESIGN.md) record physical W25Q32 checks, the provisional versioned/CRC-protected codec, publication ordering, a dedicated bounded queue/drop policy, and T-23 instrumentation. The device layer and host-tested codec are implemented; application event logging remains open.
- [x] The `4b` GPIO and HAL-test fixture uses GPIO5↔GPIO6. The first attempt exposed a GPIO7↔GPIO6 wiring mistake; after moving GPIO7 to GPIO5, `4b` passed 25/25 and the corrected HAL sketch passed 23/23 with zero skips on 2026-10-17. Production firmware was restored afterward.
- [x] UART regressions with GPIO1↔GPIO2 on 2026-10-17: `4c-uart` 8/8, `t08` 26/26, and `t09` 5/5. Additional no-module stages passed: `t04` 13/13, `t03` 6/6, `t10` 7/7, and corrected `t06` 7/7. `4c-mag` remains unavailable without the GY-511 module.
- [ ] T-23 cannot be claimed from the current µT-Kernel application placeholder: `ApplicationTask` in `runtime_mtk.c` only drains `app_mbf`. The flash device layer exists, but the application event pipeline and concurrent run are still needed.
- [ ] Team review, clean-clone reproduction, event-log/combined-workload proof, and merge remain open. Physical device and absent-device checks are linked above. Existing unrelated Task 7 deletions remain local and were not included in the D2 checkpoint; the optional TFT diagnostic sketch was removed because it was not D2 acceptance evidence.

## Professor feedback incorporated

The team-wide [Week 6 feedback tracker](WEEK6_FEEDBACK_RESPONSE.md) is the source for feedback IDs and cross-deliverable ownership. Its D2 section was a snapshot at commit `548d2e3`; use the checks and evidence below for current D2 status. F9/F10 belong to D6, F11 to D4, and R4 is shared with D4; D2 must coordinate interfaces and concurrent testing without claiming those deliverables complete.

- [x] Retain one shared SPI2 host protected by the bounded µT-Kernel mutex in `hal_mtk.c`; the earlier FreeRTOS mutex applies only to the historical Arduino backend.
- [ ] Keep flash logging out of `SchedulerTask`; perform it through `ApplicationTask` so storage cannot directly block sensor acquisition.
- [ ] Define the capacity, timeout, overflow response, and drop policy for every queue used by logging or benchmarking.
- [x] Record median, p95, p99, and maximum for five 8–1024-byte **µT-Kernel loopback SPI transfer** runs; this is transport-only evidence (NFR-08/T-19), not W25Q32 write or live-sampling timing.
- [ ] Measure p95 and maximum **live sampling interval and flash-write latency** during D2 T-23, including skipped samples; compare against the D5 NFR1 baseline (F16/R4).
- [ ] Demonstrate inference/model activity and SPI flash writes alongside live sampling (D2 T-23/R4). Coordinate, but do not substitute, D4's separate model-upload-during-sampling and chunk-order checks (F11/R4).
- [ ] Align the D2 stored-event identifier with D6's canonical event ID and persistence decision before freezing the flash record (F9/F10); D2 does not implement MQTT de-duplication or define the occupancy reset policy.
- [ ] Replace generic board/placeholder artwork with a per-node wiring diagram that shows the actual ESP32-S3, W25Q32, power, ground, signals, and final GPIOs.
- [ ] Confirm that failed or delayed SPI logging never stops occupancy processing or causes a valid-looking reading.

## D2 migration gate for Change Requirement 3

The [pre-tribunal and live-discussion context](TRIBUNAL_1_CHANGE_REQUIREMENT_3_CONTEXT.md) records team decisions and open risks; it was not evidence that the course port worked on this ESP32-S3. Since then, the migrated D2 SPI backend has built and passed the target checks linked above. The checklist below retains the original migration gates for traceability; checked historical Arduino items do not substitute for the µT-Kernel evidence. The team-wide runtime migration is led by Hasif, while D2 still needs an agreed application-log integration owner.

### Without the W25Q32 module

- [ ] Inspect the actual course µT-Kernel 3 SMP source/build and verify ESP32-S3 boot, GPIO/SPI, timing, SMP/core assignment, priority semantics, mutex/communication options, memory and networking compatibility. Record unsupported features and a tested fallback. Resolve with the lecturer whether vendor-internal FreeRTOS use is permitted; no application task or synchronization may remain on FreeRTOS.
- [ ] Audit `runtime_s3.cpp`, `hal_esp32s3.cpp` and D2 callers for FreeRTOS/Arduino dependencies. Reconcile the report's “6 tasks/5 queues” against the repository's actual task/object inventory, including MonitorTask and the proposed D2 `log_q`; assign each object a new-kernel owner and equivalent only after the port is inspected.
- [ ] Preserve the portable `bus_spi`/HAL contract, but implement a µT-Kernel-compatible ESP32-S3 SPI backend if the current `SPIClass`/FreeRTOS adapter cannot run. Replace the application SPI mutex with a supported bounded lock; verify cross-core serialization, priority inversion behavior, timeout reporting, cleanup, and no false valid sample. Do not assume DMA, ISR routing or priority inheritance without evidence.
- [ ] Migrate the proposed dedicated log handoff to a bounded course-kernel mechanism with provisional capacity 8, nonblocking producer, drop-newest counter, and `ApplicationTask` as consumer. Check object-creation results and maximum item size; keep logging out of `SchedulerTask` and occupancy changes single-writer. Coordinate canonical event IDs and late/duplicate behavior with D6.
- [ ] Keep developing portable record encode/decode, CRC golden vectors, recovery scans and mocked W25Q32 faults. Mock success is not physical flash or µT-Kernel proof.
- [ ] After the base port boots, rerun D2 config/negative, loopback, fault, lock-contention, 8–1024-byte timing and repeated open/clear tests on the new kernel. Compare p50/p95/p99/max, throughput, errors, drops and heap/stack with retained FreeRTOS logs; record any changed limits and the approved performance threshold.

### With the W25Q32 module (final proof on migrated firmware)

- [ ] Verify exact part/voltage/pins and an approved scratch sector before power-up; photograph the final processing-node wiring. Do not erase unknown flash contents.
- [ ] Pass real JEDEC/status/read/program/readback/reboot-recovery and disconnected-device tests through the migrated backend; reject absent/corrupt replies and never report an unverified stored event (T-20/T-21).
- [ ] Run the combined W25Q32 logging + live sampling + inference workload and, with Cody, asynchronous LCD/output and duplicate/delayed-event checks. Show bounded lock waits and log-queue overflow/drop accounting; a log failure must not corrupt occupancy counting or stop monitoring (T-23). Compare timing and skipped samples against the pre-change baseline.
- [ ] Retain µT-Kernel boot/task/core/priority logs, communication/lock mapping, relevant source/build configuration, test logs and before/after performance analysis for the Week 13 repository evidence. Team-wide proof of *all* application functionality belongs to the integration owners; D2 signs off only when its migrated SPI/logging path is included.

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
- [x] Keep device-specific commands out of the generic SPI transport. The W25Q32 command/device layer and provisional log codec are implemented above it; application integration is pending.

## Definition of done

The checked items in this section were verified on the **pre-change** firmware. They remain valid historical design and baseline evidence, but must be revalidated on µT-Kernel where they depend on the platform backend, mutex, task scheduling or timing. D2 is not done merely because the old loopback suite passed.

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

- [ ] Confirm the exact chip-top part marking and electrical requirements from legible module evidence; the breakout is in hand, the user checked 3.3 V wiring, and its JEDEC ID is `EF4016`.
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
- [x] Build and verify the separate W25Q32 real-device fixture for JEDEC ID, read, one-time scratch program/readback, retention and absent-device checks. Event-log proof remains open.
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
- [x] Before Change Requirement 3, the team retained `SPIClass` for the Arduino/FreeRTOS baseline. This decision does not approve `SPIClass` for the µT-Kernel build.
- [x] Replace the Arduino adapter in the µT-Kernel firmware with the register-level C SPI2/FSPI backend in `esp32s3_mtk/components/edge_mtk/hal_mtk.c`, retaining the portable `hal_spi_*` contract. The target build, physical W25Q32 checks and loopback regression pass. The old C++ `SPIClass` adapter remains a historical Arduino baseline only.
- [x] Maintain one host record per bus/pin combination and one device record per CS/frequency/mode.
- [x] Serialize transactions with a µT-Kernel `TA_INHERIT` mutex on the migrated backend. The historical Arduino backend used a FreeRTOS mutex.
- [x] Reject incompatible host reuse instead of silently reconfiguring an active bus.
- [x] Publish RX data only after a complete successful transfer.
- [x] Release device references and end the host after the final reference closes; the reusable mutex remains allocated once, with stable heap thereafter.
- [ ] Finalise DMA, alignment, maximum transfer, heap and clock limits. The migrated backend is synchronous, polled, has no DMA, uses 64-byte FIFO chunks with CS held across up to 1024 bytes, and tested 10/20 MHz loopback plus 1 MHz flash and 1/40 MHz boundary cases. Heap was stable at 92,716 bytes across the five target benchmark runs. The team-approved production clock maximum and alignment statement remain open.

## Phase 4 — automated functional and fault tests

Results checked below include pre-change evidence unless a µT-Kernel log is explicitly cited. The 25 October migrated-build evidence is linked near the top; event-log acceptance remains open.

- [x] Add `parity/spi_test.py` and a `sh tools/s3.sh spi` entry point.
- [x] T-17: configure SPI through the REPL, confirm `CFG: ok`, `SHOW`, scheduler summary, and `bus_spi` execution.
- [x] T-18: validate host/pin/clock/mode settings, compatible bus sharing, and incompatible reuse refusal.
- [x] T-19: deterministic loopback payloads of 8, 16, 32, 64, 128, 256, 512, and 1024 bytes passed 1,000 transfers each through the existing SPI HAL in five configurations: modes 0/1/2 at 10 MHz and modes 0/3 at 20 MHz. Production firmware was restored after both benchmark sessions.
- [ ] T-20: the µT-Kernel backend identifies, reads, programs, verifies and retains data on the physical W25Q32. The provisional log allocator programmed and recovered one **synthetic rejected-event record** at `0x3FF100`, including a cold power cycle and valid CRC. Retrieval of a **real processed application event** remains open until the D3/D6 handoff exists.
- [ ] T-21: the µT-Kernel 22/22 configuration/loopback suite and 6/6 connected faults passed; the physical W25Q32 layer rejected a fully disconnected module with `HAL_ERR_ABSENT (-9)`, and host mocks cover wrong ID, timeout, stuck busy, partial program and bad readback. A physical corrupted-reply and target SPI mutex-contention demonstration remain open. Generic raw SPI still has no automatic device-presence signal, so application paths must use the W25Q32 checks before claiming a stored event.
- [x] T-22: 100 initialize/deinitialize cycles passed with heap 95,748 B before and after; failed incompatible reconfiguration left existing devices usable.
- [ ] T-23: run W25Q32 logging concurrently with live D1 sensor sampling and inference, with no reset, watchdog, missed sample, or unexplained corruption; report p95/max intervals and flash-write latency. D4 separately owns model-upload-during-sampling evidence.
- [x] Add deliberate byte-for-byte mismatch detection to the loopback tests rather than treating received zeros as success.

## Phase 5 — benchmark and retained evidence

The checkbox items below originally describe the FreeRTOS-era comparison baseline. The separate [µT-Kernel five-run evidence](#25-october-target-benchmark-evidence) now matches the same clock/mode/payload matrix; continue to label both runtimes in every comparison.

- [x] Run 10 warm-ups and 1,000 measured transactions per size at 10 MHz, mode 0.
- [x] Record median, p95, p99, maximum latency, and effective payload throughput.
- [x] Record attempts, platform errors, timeouts, mismatched transactions and bytes, observed resets, and heap before/after.
- [x] Report transaction error rate and byte error rate separately.
- [ ] Record SPI mutex wait/hold time, sampling jitter, queue high-water/overflow behaviour, and per-task stack high-water marks during the concurrent run.
- [ ] Separate transport-only loopback percentile evidence from live W25Q32 program/readback latency and sampling p95/max; do not use the former to claim the latter.
- [ ] Approve and implement logging/benchmark queue capacities, timeout behaviour, overflow response, drop counters, and high-water diagnostics before the concurrent test. The provisional log-queue policy is documented in [D2_EVENT_RECORD_DESIGN.md](D2_EVENT_RECORD_DESIGN.md); it has not been tested.
- [x] Store raw serial output, 8,000 CSV latency samples, JSON metrics, and a concise result summary under `parity/logs/d2-spi/bench-20261017-140138/`.
- [ ] Record commit, ESP32-S3FH4R2/HW-747 board, Arduino CLI/IDE, core 3.3.12, ESP-IDF version, FQBN, CPU frequency, host, pins, mode, clock, backend, tester, and date.

## Phase 6 — exact D1 regression gate

The completed checks below predate the RTOS change. Re-run affected D1 and D2 stages on the µT-Kernel firmware and compare outcomes rather than carrying their pass status forward.

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
- [ ] Before final D2 sign-off, link the migrated SPI/logging evidence and the team-wide Week 13 task/core, communication/synchronization, and correctness/performance documents; do not present FreeRTOS-era logs as µT-Kernel proof.

## Remaining decisions before finalisation

- [x] Use jumper loopback as the initial RX/integrity proof; add W25Q32 as the required real-device proof.
- [x] Confirm the six flash-module labels and the GPIO10/11/12/13, 3V3 and GND wiring before the powered test; Brendan manually traced both ends. The top chip marking remains unreadable.
- [x] The team kept the thin Arduino C++ `SPIClass` HAL adapter for the pre-change baseline only.
- [x] Re-decide backend choice after inspecting and testing the course µT-Kernel port: the migrated build uses register-level SPI2 and the µT-Kernel mutex behind the unchanged EdgeCore HAL contract.
- [ ] Obtain team sign-off for normal scheduled TX/RX limits and the separate 1024-byte benchmark path.
- [ ] Approve maximum SPI clock and numerical performance thresholds.
- [ ] Confirm that queued/streaming SPI is deferred from D2.
- [ ] Reconcile ownership: the earlier D2 allocation names Brendan/Jy, while the Week 6 review assigns SPI drivers to Sitt and configuration/timing to Brendan.
