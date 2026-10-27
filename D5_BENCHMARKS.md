# D5 Benchmarking Programme on ESP32-S3 µT-Kernel 3 SMP

D5 is "Define and execute the benchmarking programme" in the team's requirements document (owners: Hasif and Sitt). This file holds the benchmark definitions and the measurement method; the task checklist and its status are in [README section 6](README.md#6-d5-task-checklist). **This file is a plan. It contains no final D5 results.**

Status (28 October 2026): the plan is based on `main` at `8ee7b6f`, which holds D1 on the course µT-Kernel 3 SMP port ([`esp32s3_mtk/`](esp32s3_mtk/)) and the D2 SPI checkpoint. All application tasks, scheduling, inter-task communication and synchronisation run on µT-Kernel, as the professor's Change Requirement 3 requires, so every D5 measurement is taken on that firmware. Existing D1 and D2 logs are pilot and reference data; final D5 values need the approved method below and repeated hardware runs.

## 1. What D5 must deliver

The requirements document sets D5's work and its evidence.

**Work.** Define what each benchmark measures and why it matters to the configurable edge claim, then build repeatable tests with a fixed method and enough repeats to give a stable average. The programme measures the cost of configurability as well as its benefit. All results are recorded on real hardware, and no placeholder value is reported as a final result.

**Evidence.** Benchmark definitions and method documented well enough for another member to repeat them; completed result tables for B1 to B6 as a minimum (B7 and B8 if time allows); and a summary of where configurability costs performance and where it does not.

The professor's Week 6 feedback adds three items to D5 ([tracker](WEEK6_FEEDBACK_RESPONSE.md)): SchedulerTask cycle measurement (F6), sampling-interval tails rather than means (F16), and the risk of a 50 ms window with every sensor configured (R1). The requirements document also gives D5 one cross-cutting requirement: **explicit fault reporting**. A sensor timeout, a link loss or a missing or unsupported model must report an error rather than valid-looking data, evidenced by fault injection during the B8 run.

## 2. The benchmarks

B1 to B8 are the requirements document's benchmark programme; each tests one claim the platform makes. B1 to B6 are the minimum set; B7 and B8 are run if time allows.

| ID | Benchmark | Primary metrics | What it proves |
|---|---|---|---|
| B1 | ESP32-S3 portability | Common API pass rate by feature | The framework ports without application logic being rewritten |
| B2 | SPI performance | Latency, throughput, transfer errors by payload size | The new SPI support is correct and usable |
| B3 | Configurable timing | Mean period, timing error, jitter | The fixed one-second delay is genuinely replaced by controlled timing |
| B4 | Runtime reconfiguration | Apply latency, success rate | The core configurable-edge feature works at runtime |
| B5 | ML efficiency | Accuracy, model size, RAM, inference latency | Optimisation is quantified rather than claimed |
| B6 | End-to-end use case | Event accuracy, false events, occupancy count error | Sensors, configuration and ML work as one system |
| B7 | Resource usage | Flash, RAM, CPU where measurable | The cost of configurability and ML is known |
| B8 | Reliability and stress | Missed samples, errors, resets, packet loss | The system is stable over sustained multi-sensor operation |

### What each benchmark is compared against

There is no single baseline. Each benchmark is compared with whatever its claim needs, and every comparison is labelled in the results.

| ID | Compared against | Measurement plan | Pilot or reference data already in the repository | Depends on | Can start |
|---|---|---|---|---|---|
| B1 | The Pico reference: the same parity stage gives the same output on both boards | Run every parity stage on the µT-Kernel build and mark each feature pass, partial or fail with a reason, against the Pico result | [PARITY.md §2](parity/PARITY.md#2-parity-table-task-5) (Pico) and [§4](parity/PARITY.md#4-esp32-s3-on-µt-kernel-3-smp-tasks-24-to-34) (µT-Kernel). Six stages are still hardware pending: 4b, t02 (GPIO5 to GPIO6 jumper), 4c-uart, t08, t09 (GPIO1 to GPIO2 jumper) and 4d-ir (light sensor) | D1 (done) | Now |
| B2 | Its own payload sweep (8 to 1024 bytes) with zero errors expected; for the cost of configurability, a `proto=spi` read through the scheduler against a direct HAL transfer of the same bytes; the Arduino `SPIClass` results as runtime context only | `sh tools/mtk.sh spi-bench` in the five recorded clock and mode configurations, five runs each; method in [D2_D4_RESEARCH_CONTEXT.md §4.6](D2_D4_RESEARCH_CONTEXT.md#46-b2-benchmark-method) and [D2_SPI_ESP32S3_RESEARCH.md §10](D2_SPI_ESP32S3_RESEARCH.md#10-b2-benchmark-protocol) | D2's µT-Kernel transport benchmark of 25 October: five configurations, 40,000 transfers, zero errors ([reports](parity/logs/mtk/d2-spi/)) | D2 transport (merged to `main` on 27 October) | Now (GPIO12 to GPIO13 jumper) |
| B3 | The configured period (timing error, jitter, skipped samples), and the Pico's fixed one-second loop delay (defect D-06) to show it is gone | Requested against measured interval at 10, 50, 100, 500, 1000 and 2000 ms; SchedulerTask cycle times (F6); the 50 ms all-sensors window including one and two HC-SR04 reads with no echo (R1) | NFR1 and SchedulerTask cycle tables in [PARITY.md §4](parity/PARITY.md#4-esp32-s3-on-µt-kernel-3-smp-tasks-24-to-34) (`timing`, `wcet`) | D3's timing modes (one-shot, event-driven, logical sequence) | Periodic baseline now; final after D3 |
| B4 | Rebuilding and reflashing: changing behaviour at runtime against editing code, building and flashing | At least 20 valid and invalid sensor, rate, rule and model changes over USB and MQTT; apply latency, success rate, sampling interruption and heap change, against the time, steps and outage of the equivalent rebuild and flash | None yet (new script) | None | Now |
| B5 | D4's baseline model against its optimised model, on the same inputs and build settings | Protocol in [D4_EMBEDDED_ML_RESEARCH.md §13](D4_EMBEDDED_ML_RESEARCH.md#13-b5-benchmark-protocol) and [D2_D4_RESEARCH_CONTEXT.md §5.7](D2_D4_RESEARCH_CONTEXT.md#57-b5-benchmark-method); inference also measured while sensors and MQTT run | The built-in autoencoder on `main`: 500 to 525 µs per invocation, arena 2,908 of 65,536 B ([PARITY.md §4](parity/PARITY.md#4-esp32-s3-on-µt-kernel-3-smp-tasks-24-to-34)) | D4's two models | After D4 |
| B6 | Ground truth: the system's events and occupancy count against a manual count; rule-based against ML on the same event set (D6) | Trials covering entry, exit, incomplete or turn-around, and false or object events | None | D6 workload (and D3, D4) | After D6 |
| B7 | For the cost of configurability, the fixed path on the same build (section 3); the Arduino build and the Pico as context only | Flash and static RAM (`idf.py size`), internal heap free and minimum with Wi-Fi running, PSRAM use, stack minima (`TASKS`), CPU load where measurable | Static internal RAM 97.9%; about 34 KB of internal heap free with Wi-Fi and MQTT running ([d2-merge logs](parity/logs/mtk/d2-merge/)) | None | Now |
| B8 | Zero: no resets, watchdog events or unexplained missed samples over a sustained multi-sensor run; agreed limits for errors and packet loss | Three 30-minute runs with sensors, rules, inference and MQTT, with fault injection during the run: sensor disconnected or timing out, Wi-Fi and broker loss, missing, corrupt or oversized model. Every fault must be reported as an error and never published as valid data | `endurance` 6/6 over 30 minutes, `faults` 9/9, `flood` pass, `t06` no-echo timeouts ([parity/logs/mtk/](parity/logs/mtk/)) | D6 for the final workload | D1-only workload now (labelled as such); final after D6 |

**Update this table** when D3, D4 or D6 is merged into `main`: replace "After" in the last column with the date, and link the merged workload.

## 3. The cost and the benefit of configurability

This is the main D5 claim, and the summary the requirements ask for.

**Benefit (B4).** Changing a sample rate, a pin, a rule or a model at runtime is compared with making the same change in code and building and flashing. The result is the time, the number of steps and the outage of each.

**Cost (B2, B3, B7).** The same sensor work is done twice on the same firmware: once through the configuration framework (a `BEGINCFG` line, the scheduler and the bus registry) and once through a fixed path with the settings built in. The differences in latency, cycle time, RAM and flash are the cost of configurability. The proposed fixed path is a small set of BenchmarkTask console commands that read the same sensors directly through the HAL, as D2's `SPIBENCH` already does for SPI, rather than a separate fixed-configuration firmware. The separate firmware is the stronger control but costs a second build to maintain; the choice is made in task D5-1.

The Arduino/FreeRTOS build in `esp32s3_edge/` measures a different thing: the effect of the RTOS change, not of configurability. D1 already recorded that comparison ([PARITY.md §4](parity/PARITY.md#4-esp32-s3-on-µt-kernel-3-smp-tasks-24-to-34)); D5 uses it only as labelled context.

## 4. Dependencies and known gaps

| Item | State on 28 October 2026 | Effect on D5 | Update when |
|---|---|---|---|
| D1 | Done on µT-Kernel (tag `d1-mtk-v1.0`). The model test records 8/9 because the test reopens the port after the model has already reloaded; the reload is captured separately. Six stages still need jumpers or the light sensor | B1 includes those six stages | They are run |
| D2 | Merged as a checkpoint. Transport and W25Q32 device layer proven on µT-Kernel; flash event logging from ApplicationTask and its T-23 concurrent run wait for D3 | B2 measures the transport now; SPI logging under load joins B8 later | D2 adds event logging |
| D3 (Cody) | On branch `d3`, last updated 18 October, still on the Arduino runtime ([D3_TASKS.md on `d3`](https://github.com/junyonggoh2004/EMBSYS-P2-AAI15/blob/d3/D3_TASKS.md)) | B3's final run needs its timing modes on µT-Kernel | D3 merges: update B3 |
| D4 (Brendan, Jun Yong) | Research and a B5 protocol; no models on `main` | B5 cannot run | Both models are deployable: update B5 |
| D6 (whole team) | Entrance workload and event set not frozen; part of its application logic is on `d3` | B6 cannot run; B8's final workload waits | The workload is frozen: update B6 and B8 |
| Equipment | Physical input-to-output latency needs one external clock, such as a logic analyser or a second board timestamping both edges | Without it, latency uses on-board timestamps, and the results say so | Confirmed in task D5-1 |
| Energy | Optional until a calibrated meter, wiring and sampling interval are available | Never inferred from timing or reported unmeasured | A meter is available |

## 5. Measurement method (approve before final runs)

1. **Freeze the system.** Record board, module and flash/PSRAM identity, power source, wiring and stimulus, sensor and model hashes, firmware commit and binary hash, course-port commit, ESP-IDF, compiler and component versions, build options, the task-to-core table, the broker and network configuration and the host clock. `sh tools/mtk.sh check` and [SETUP.md](SETUP.md#esp32-s3-on-µt-kernel-3-smp-tasks-2534) record the environment.
2. **Freeze each workload.** Version the exact configuration lines, stimulus pattern, input rate, model, MQTT payload and rate, warm-up and run duration. Specify which real sensor or electrical fixture is connected. Reset or clear state in a fixed order, and alternate the run order of paired conditions. When switching from µT-Kernel to the Arduino baseline, erase the flash first as SETUP.md describes, because the two builds' model and LittleFS partitions overlap, and load the model again before comparing. Keep D3, D4 and D6 features out of an integrated claim until their interface and workload are frozen.
3. **Measure the intended interval.** Use on-board monotonic timestamps for scheduler, configuration, inference and message-buffer operations, and one external clock for physical input-to-output latency. Timestamp MQTT at both defined endpoints and distinguish application enqueue, wire delivery and broker receipt. Do not use the host's serial display as a real-time clock. Time the measurement code itself, including the `bench_mbf` path, and record its capacity and policy (16 messages; senders never wait, and a full buffer drops the new message and counts it). Do not print per event inside a timed section. The `TASKS` cycle maximum is the largest observed cycle, not a proven worst-case bound.
4. **Repeat complete runs.** Proposed for sign-off in task D5-1: at least five independent runs per condition after warm-up; at least 100 events per run for interval and latency metrics; at least 20 independent configuration changes; at least three 30-minute sustained runs. Report run count, event count, mean, median, standard deviation, p95, maximum and error and drop counts. If adding the final run moves the cumulative mean by more than 5%, investigate and extend the series. Keep and explain outliers and failed runs.
5. **Fix acceptance limits before collecting final data.** Carry the D1 non-functional requirements forward as references, then agree D5 limits for deadline misses, tails, throughput, memory and reconfiguration benefit. Do not turn a pilot observation into a target after the fact. Flag any conflict with D1, D3, D4 or D6 requirements for team review.
6. **Keep raw evidence.** Save each run's timestamped observations, setup manifest, script version, outcome and notes, including invalid and aborted runs. Generate tables and plots from the raw files. Do not copy one-off D1, D2 or Arduino results into D5's final columns.

## 6. Where the work goes

Create these under `benchmarks/` when measurement starts: `README.md` (the approved protocol and limits), `fixtures/` (configuration lines, stimulus patterns and the fixed-path commands), `scripts/` (capture and analysis, wrapping the existing `parity/` scripts and `tools/mtk.sh`), `raw/` (per-run data and manifests) and `results.md` (the B1 to B8 result tables, each number linked to its hardware, commit, script, run files and calculation, and the configurability cost and benefit summary). The existing D1, D2 and Pico files stay unchanged.

## 7. Earlier planning IDs

The first draft of this file (Sitt, 24 October) used local IDs D5-M01 to M07, because the B1 to B8 definitions were not yet in the repository. They map onto the benchmarks as follows: M01 (scheduler timing and SMP behaviour) is B3; M02 and M03 (configuration cost and benefit) are B4, with the cost side measured in B2, B3 and B7; M04 (peripheral and action latency) is B2 for SPI and B3 for GPIO, I2C and UART; M05 (MQTT under load) is part of B8; M06 (inference cost) is B5; M07 (resources and sustained operation) is B7 and B8. B1 and B6 had no draft equivalent and were added on 28 October.
