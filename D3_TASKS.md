# D3 Configurable Timing Task List

Contract and integration owner: **Cody**  
Implementation owners: Sitt for sensor/hardware timing, Brendan for SchedulerTask sequencing/ordering, Hasif for platform/runtime and RTOS/queue benchmarking  
Branch: `d3`, including the current D1/D2 `main` baseline
Status date: 2026-10-28 (runtime-port update; original checklist retained below)

## 28 October: micro T-Kernel ApplicationTask port

- [x] Add root `AGENTS.md` with ownership, runtime, RAM and verification boundaries.
- [x] Replace the application message-draining placeholder with typed event/reset/status consumption; register the EventRecord sink before producers start.
- [x] Use zero-wait message sends and existing per-buffer drop accounting; keep all occupancy/reset mutation in ApplicationTask.
- [x] Preserve core 1, priority 23 and 4 KiB stack. Keep kernel message storage internal (192-byte growth in the native layout; verify target size with `TASKS`) and processing/formatting state in EdgeCore BSS mapped to PSRAM.
- [x] Offer each processed result to the existing publisher, record enqueue failure independently of occupancy, and retain BenchmarkTask's SPI commands.
- [x] Native dispatch tests cover result publication/refusal, duplicate/stale/invalid outcomes, ordered reset and malformed commands/messages. Existing policy behavior is retained, not certified as the final application contract.
- [ ] Build on the pinned SDK, inspect the linked image and PSRAM allocation, then run 4a, 5-neg, MQTT and synthetic application smoke cases. Use `TASKS` to prove core residency, stack/heap margin and full-buffer drops.
- [x] Fix reviewed one-shot self-rearming, periodic timeout recovery, disabled-stream and deadline semantics in the shared C implementation; retain native regression evidence (see the timing update below).
- [ ] Settle identity, cross-node clocks and quality validation. The task-glue and timing fixes do not close those application-contract findings.
- [ ] Agree D2 field mappings and implement its bounded log handoff; no application flash writes are introduced by this port.

The original phase checkboxes below record earlier work and remaining evidence.
The typed Arduino handoff is now also implemented in micro T-Kernel source, but
no target/hardware checkbox is promoted on native-test evidence alone.

## 28 October: shared timing correctness update

- [x] Keep timing behavior in EdgeCore and platform/kernel objects below the HAL boundary. Pico's manifest lists the shared modules; the IDF build discovers them with `CONFIGURE_DEPENDS`.
- [x] Separate explicit `TRIGGER` from completed-sample notifications. Automatic handoff arms only another event-driven sensor with a matching `timing.trigger_source`; a one-shot cannot rearm itself by publishing.
- [x] Consume a periodic timeout by advancing to the next future anchored slot, without a catch-up burst. Non-periodic timeouts complete until explicitly triggered again.
- [x] Honor `timing.enabled=0` for stream startup, polling and scheduler wakeups.
- [x] Apply the earlier applicable nonzero timeout/window bound to lateness plus acquisition. Completion exactly at the deadline is accepted; a late synchronous response is discarded before rule/publication/source-handoff processing. This reports a timeout; it cannot cancel or shorten a blocking driver call.
- [x] Validate timing before hardware installation and reset the RUN-edge state on scheduler clear.
- [x] Add native integration tests linking the real scheduler, timing state and output publisher with mocked hardware/network/rule consumers. Cover one-shot rearm, periodic recovery/skip-ahead, event-source handoff, deadline boundaries, invalid config, disabled streams and clear/reconfigure.
- [ ] Run these behaviors on the pinned Pico/S3 toolchains and active micro T-Kernel board, then measure the full entrance workload. The 50 ms interpretation, asynchronous sonar capture, crosstalk, skipped/deadline metrics and hardware acceptance remain open with their owners.

D3 covers periodic, one-shot, event-driven and logical-sequence timing. The professor feedback makes the entrance path the acceptance case: radar presence opens an observation window, two HC-SR04 sensors are sampled in a defined order without blocking SchedulerTask or causing crosstalk, and the timing is proven under the complete workload.

This checklist is the authoritative execution list for D3. Update the related feedback items in [WEEK6_FEEDBACK_RESPONSE.md](WEEK6_FEEDBACK_RESPONSE.md) only after the evidence named here exists.

## Ownership rule

- **Cody directly owns:** D3 timing modes/contract, event-window rules, EventRecord/ApplicationTask timing consequences, acceptance criteria and interpretation of integrated evidence.
- **Cody coordinates:** hardware/sensor timing with Sitt, SchedulerTask sequencing/ordering with Brendan and platform/runtime measurements with Hasif.
- **Cody does not directly own:** low-level capture/UART drivers, all benchmark implementation or platform internals.

An item may appear in this D3 checklist without being implemented by Cody. For each such item Cody freezes the application-facing requirement and accepts or rejects the resulting evidence; the named module owner implements and measures it.

## Responsibility summary

| Work area | Direct owner | Cody's role |
|---|---|---|
| Timing modes, observation-window semantics and acceptance criteria | Cody | Define and approve. |
| EventRecord/ApplicationTask timing consequences | Cody | Define and validate. |
| HC-SR04 capture and sensor-level timing | Sitt, with Hasif platform support | Specify required outcomes; coordinate and interpret evidence. |
| SchedulerTask sequencing and ordering | Brendan | Specify application contract; review ordering/WCET consequence. |
| RTOS, queue, stack, heap and platform benchmarking | Hasif | Define required evidence and acceptance threshold; interpret results. |
| Physical sensor timing/crosstalk measurements | Sitt | Define test scenarios and interpret whether D3 contract is met. |

## Status legend

- `[x]` implemented and supported by retained evidence
- `[-]` partly complete or code-ready but still needs a decision/hardware result
- `[ ]` open
- `[B]` blocked by a named dependency

## D3 completion rule

D3 is complete only when all of the following are true:

1. Periodic, one-shot, event-driven and logical-sequence modes have frozen semantics and validation.
2. HC-SR04 echo timing does not busy-wait inside SchedulerTask.
3. Two HC-SR04s have a measured trigger order and spacing that avoids crosstalk.
4. The complete entrance workload meets its agreed timing limits with p95, p99, maximum, skipped/deadline-miss and WCET evidence.
5. Timeout, missing-sensor, queue-overflow and invalid-configuration cases are explicit and do not appear as valid readings.
6. D1 regression tests still pass on the exact D3 commit.
7. The feedback tracker, architecture, setup/configuration documentation and retained logs agree.

## Phase 1 - Freeze the contract

### D3-01: Confirm requirements and measurement boundaries

- [-] Map D3 to FR10, FR11, FR12, FR15, NFR1 and the current parity tests.
- [ ] Define what starts and ends the 50 ms requirement: radar event, first trigger, sample deadline, EventRecord closure or publication.
- [ ] Decide whether 50 ms means each ultrasonic sensor is sampled every 50 ms or the pair completes one sequence every 50 ms.
- [ ] Define the observation-window duration and what closes it.
- [ ] Define timing pass criteria for mean, p95, p99, maximum, missed deadlines and skipped samples.

Evidence: approved D3 requirement table linked from `parity/PARITY.md` and the feedback tracker.

### D3-02: Freeze mode semantics

- [x] Periodic: first execution is immediate on `RUN`; deadlines use `interval_ms`; missed periods skip ahead instead of bursting.
- [x] One-shot: `RUN` or `TRIGGER` arms exactly one execution; a second trigger while armed is refused; a completed sensor can be re-armed explicitly.
- [-] Event-driven: a matching completed `timing.trigger_source` sample or `TRIGGER` arms one execution; window validation and duplicate-arm refusal are implemented, but sensor-specific debounce and queue saturation evidence remain open.
- [-] Logical sequence: timestamp/event-driven source-to-target hand-off exists; the two-HC-SR04 A/guard/B state machine remains Brendan/Sitt work.
- [ ] Decide how runtime reconfiguration affects an active observation window.

Evidence: state table plus positive/negative examples in the configuration documentation.

### D3-03: Freeze configuration fields and validation

- [x] List every new configuration key, type, unit, range and default.
- [x] Reject unsupported mode/protocol combinations before changing active hardware.
- [ ] Validate that trigger/echo pins differ and that both sensors do not conflict with reserved or active pins.
- [-] Bound periods, timeouts and window durations; pair guard intervals still await the physical design. Queue depths remain the D1 provisional values.
- [x] Refuse an invalid block before it is installed; existing active sensor slots remain unchanged.

Owner boundary: Cody approves timing semantics; Brendan implements parser/SchedulerTask changes; Sitt/Hasif approve GPIO capability and pin constraints.

## Phase 2 - Establish the baseline

### D3-04: Record current blocking behaviour

- [x] Identify `measure_pulse_us()` in `libraries/EdgeCore/src/bus/gpio.c` as a SchedulerTask busy-wait.
- [x] Record the configured/default pulse timeout and the two-sensor worst-case calculation in `WEEK6_FEEDBACK_RESPONSE.md`.
- [-] SchedulerTask cycle instrumentation and `parity/wcet_test.py` exist.
- [ ] Run the six WCET scenarios on the target board and retain the raw log.

Evidence: current implementation reference, `TASKS` WCET output and the test log. This baseline is evidence of the problem, not D3 completion.

### D3-05: Record current scheduler accuracy

- [x] `parity/timing_test.py` reports mean, p95, p99, maximum and skipped samples.
- [x] Retained D1 timing evidence covers 10, 50, 100, 500, 1000 and 2000 ms configurations.
- [ ] Repeat the timing run with the real entrance workload and the D3 implementation.

Evidence: before/after timing tables on the same board, toolchain and test duration.

## Phase 3 - Remove blocking echo timing

### D3-06: Select the capture mechanism

- [ ] Compare timestamped GPIO edge interrupts, RMT RX and MCPWM capture against the required pulse range, simultaneous channels, API stability and C/HAL boundary.
- [ ] Prefer the smallest mechanism that provides rising/falling timestamps without holding SchedulerTask.
- [ ] Record why the selected mechanism is adequate and why the alternatives were not needed.
- [ ] Confirm whether one capture channel/context is needed per sensor and how cancellation/reset releases it.

Contract decision owner: Cody. Platform/driver feasibility and implementation: Sitt/Hasif. Do not introduce direct Arduino/ESP-IDF calls into EdgeCore.

### D3-07: Define the portable asynchronous pulse contract

- Cody defines the application-visible outcomes and timing bounds; Sitt/Hasif implement the low-level HAL contract with Brendan reviewing its scheduler use.
- [ ] Add a HAL/platform contract for arm, completion/status, timeout/cancel and cleanup.
- [ ] Return typed outcomes: complete pulse, still pending, timeout, invalid configuration, busy/conflict and platform error.
- [ ] Timestamp edges with the monotonic microsecond clock.
- [ ] Guarantee no callback mutates SchedulerTask state directly; notify or queue a bounded event instead.
- [ ] Define behaviour for an already-high Echo pin, missing rising edge, missing falling edge and spurious extra edges.

Evidence: interface review, host/mocked tests and HAL boundary check.

### D3-08: Implement the ESP32-S3 capture backend

Implementation owner: Sitt/Hasif. Cody coordinates the timing contract and accepts the evidence.

- [ ] Allocate and configure the selected capture resource per sensor.
- [ ] Keep ISR/callback work bounded and allocation-free.
- [ ] Notify SchedulerTask or a driver-owned state object without busy-waiting.
- [ ] Implement timeout and cancellation without leaving interrupts/channels active.
- [ ] Implement cleanup through the existing `bus_t.deinit` lifecycle.
- [ ] Expose diagnostic counters for arm, complete, timeout, spurious edge, overflow and conflict.

Evidence: target-board pulse/no-pulse tests, repeated configure/clear test and stable heap/resource counts.

### D3-09: Convert the shared GPIO pulse driver to a state machine

Implementation owner: Sitt/Hasif for the driver and Brendan for SchedulerTask interaction. Cody reviews application-visible timing/error semantics.

- [ ] Remove the blocking Echo loops from the active ESP32-S3 scheduling path.
- [ ] Trigger once, return pending, and publish only after a complete valid pulse.
- [ ] Ensure timeout/error is reported and never encoded as a valid distance.
- [ ] Prevent a second trigger while the same sensor is pending.
- [ ] Preserve Pico compatibility through the agreed HAL implementation or an explicitly documented unsupported/fallback path.

Evidence: shared-code build for Pico/S3, HAL boundary test and parity result.

## Phase 4 - Define two-sensor sequencing

### D3-10: Set the physical range and echo timeout

- [ ] Measure/approve the maximum useful entrance distance.
- [ ] Derive the maximum expected round-trip echo time from that distance plus measured margin.
- [ ] Set a timeout that is long enough for the approved range but short enough for the sequence budget.
- [ ] Reject a configuration whose two timeout/guard budgets cannot meet the requested pair period.

Do not keep a 25-30 ms timeout merely because it is the current default if the entrance geometry supports a shorter bounded range.

### D3-11: Freeze trigger order and guard interval

- Cody freezes the ordering/ambiguity contract; Brendan implements scheduling and Sitt measures physical crosstalk.
- [ ] Name the sensors consistently as outside/A and inside/B.
- [ ] Define the default order and how the initial radar/edge event may change the expected direction state.
- [ ] Fire only one ultrasonic transmitter at a time.
- [ ] Start the next trigger only after completion/timeout plus an approved acoustic guard interval.
- [ ] Decide whether the sequence alternates A/B by cycle or always starts from one side.
- [ ] Define cancellation when the observation window closes.

Evidence: state diagram, timestamped trace and physical crosstalk test.

### D3-12: Implement the logical-sequence state machine

Provisional states:

1. `IDLE`: slow monitoring; wait for valid radar presence/event.
2. `ARM_A`: configure/arm capture A.
3. `WAIT_A`: wait for complete/timeout without blocking SchedulerTask.
4. `GUARD_AB`: acoustic guard time.
5. `ARM_B` / `WAIT_B`: capture B.
6. `WINDOW_ACTIVE`: repeat the approved sequence until the window closes.
7. `COMPLETE`: hand one ordered, timestamped observation to the D6 EventRecord producer.
8. `DEGRADED`: record missing/faulted sensor quality and apply the agreed fallback.

- [ ] Cody freezes state transitions and timeout/application semantics with Brendan/Sitt.
- [ ] Keep transitions driven by timestamps/events rather than task sleeps.
- [ ] Bound the number of queued events and define overflow behaviour.
- [ ] Ensure reconfiguration/`CLEAR` cancels all armed operations safely.

## Phase 5 - Integrate radar and event hand-off

### D3-13: Accept a bounded radar-presence event

- [B] Define the D3 input contract: presence value, timestamp, validity, expiry and source quality.
- [B] Open the observation window only on a valid transition according to the agreed rule.
- [B] Ignore/rate-limit duplicate presence frames without extending the window forever.
- [B] Enter DEGRADED or close the window when radar data becomes stale.

Blocked by F8: Sitt/Brendan must implement continuous LD2450 draining and parsing. Cody owns the presence contract, not the UART driver.

### D3-14: Hand off one ordered observation

- [ ] Assemble the timing/order result on the sensor node.
- [ ] Attach source node, boot/sequence identity, window start/end, sensor timestamps and quality flags.
- [x] Send one typed hand-off to the D6 EventRecord path through the non-blocking EventRecord sink and typed `app_q`.
- [ ] Never rely on the processing node receiving raw MQTT readings in order.

Dependency: freeze EventRecord v1 in [CODY_WEEK8_SUBMODULE.md](CODY_WEEK8_SUBMODULE.md).

## Phase 6 - Verification

### D3-15: Host and mocked tests

- [x] Periodic timing arithmetic and skip-ahead behaviour.
- [-] One-shot arm and execute-once; explicit cancellation remains open.
- [-] Event-driven duplicate-arm behaviour; sensor-specific debounce/rate-limit and queue overflow remain open.
- [ ] Sequence order for A then B, B then A, timeout, incomplete and cancellation.
- [x] Invalid timing configurations are rejected before scheduler installation.
- [-] Event validation and quality flags are host-tested; full sequence timestamps await the sensor-node assembler.

Evidence: deterministic test output retained under `parity/logs/d3/` or another agreed evidence directory.

### D3-16: Single-sensor hardware tests

- [ ] Real Echo pulse returns a plausible width/distance without blocking SchedulerTask.
- [ ] No-echo returns timeout within the configured bound.
- [ ] Stuck-high, missing falling edge and spurious-edge cases report explicit errors.
- [ ] Reconfigure/clear 100 times with no progressive heap/resource loss.

### D3-17: Two-sensor crosstalk and direction tests

- [ ] Sensors mounted in the intended entrance geometry.
- [ ] Capture timestamped trigger and Echo edges for the approved order/spacing.
- [ ] Run entry, exit, incomplete/turn-around, false/object and simultaneous/ambiguous cases.
- [ ] Verify no overlapping acoustic windows beyond the approved design.
- [ ] Record false/missing detections and the resulting quality flags.

### D3-18: Full SchedulerTask WCET test

Measurement implementation: Hasif for RTOS/task/queue/platform evidence and Sitt for hardware/sensor timing. Cody defines the workload/threshold and interprets the result.

- [ ] Run `parity/wcet_test.py` with periodic sensors only.
- [ ] Add one ultrasonic success and no-echo timeout.
- [ ] Add two sequenced ultrasonic sensors.
- [ ] Add LD2450 presence plus I2C sensors.
- [ ] Add MQTT/config traffic, rules and inference.
- [ ] Report cycles, mean, p95 bucket, maximum and deadline/skipped counts.
- [ ] Demonstrate measured margin against the agreed sequence period.

Pass threshold must be frozen before the final run. A result equal to the deadline with no margin is not a defensible pass.

### D3-19: NFR1 and fault regression

Test execution is shared with the module owners. Cody decides whether the combined evidence preserves the D3 and application contracts.

- [ ] Re-run `parity/timing_test.py` with Wi-Fi/MQTT active.
- [ ] Re-run relevant HAL, GPIO, UART, I2C, rule, MQTT and negative tests.
- [ ] Inject queue saturation, sensor timeout, radar disconnect and invalid reconfiguration.
- [ ] Confirm sampling continues where safe and failures are marked DEGRADED rather than valid data.
- [ ] Run the agreed endurance test and capture stack/heap/watchdog results.

### D3-20: End-to-end D3/D6 acceptance

- [ ] A real radar/presence event opens one observation window.
- [ ] Both ultrasonic sensors are captured in the approved sequence.
- [ ] The sensor node forms one identified, timestamped observation/EventRecord.
- [ ] The processing node classifies it without reconstructing raw MQTT ordering.
- [-] ApplicationTask duplicate/reorder/occupancy semantics pass native tests; the real multi-node entrance run is pending.
- [ ] Benchmark evidence links acquisition, event closure and application result timestamps.

## Phase 7 - Documentation and sign-off

### D3-21: Update documentation

- [ ] Replace the blocking-pulse description in `ARCHITECTURE.md` with the implemented capture/state-machine design.
- [x] Document every implemented D3 configuration field and an event-driven example in the config guide.
- [ ] Add the final trigger order, spacing, timeout and sensor-node state diagram.
- [ ] Update requirement/test traceability in `parity/PARITY.md`.
- [ ] Link raw logs, commit, board, wiring, tool versions, tester and date.

### D3-22: Update the feedback tracker

- [ ] F5 becomes resolved only after non-blocking hardware evidence.
- [ ] F6 becomes resolved only after the full WCET board run and conclusion.
- [ ] F7 becomes resolved only after the order/spacing design and crosstalk evidence.
- [ ] R1 becomes resolved only after the complete 50 ms workload has measured margin.
- [ ] C1 becomes resolved only after the real entrance sampling path is demonstrated.
- [ ] Add a dated change-log row for every status/evidence update.

### D3-23: Team review and merge gate

- [ ] Cody reviews the complete D3 evidence and states whether the timing/application requirement is met.
- [ ] Sitt/Hasif approve the capture implementation and hardware evidence.
- [ ] Brendan approves SchedulerTask/configuration integration.
- [ ] Hasif confirms D1 regression and HAL boundaries.
- [ ] No credentials, generated binaries or local build output are included.
- [ ] Merge only after every required link resolves from a clean checkout.

## Immediate next actions

1. Cody, Brendan, Sitt and Hasif answer D3-01's two 50 ms interpretation questions.
2. Sitt/Hasif and Cody record the capture-backend decision and portable pulse outcomes in D3-06/D3-07.
3. Brendan/Sitt implement the non-blocking pulse and A/guard/B sequence against the timing contract.
4. Hasif runs the existing WCET baseline and repeats it under the complete entrance workload.
5. Connect a target board, run `tools/cody_test.sh`, flash `d3`, exercise rule `event:<class>` hand-off and retain the real D3/D6 logs.

