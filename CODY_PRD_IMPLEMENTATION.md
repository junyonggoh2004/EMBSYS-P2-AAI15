# Cody D1/D3/D6 PRD Implementation

Branch: `d3` (from `s3-port-d2`)  
Implementation date: 2026-10-18  
Owner: Cody for the application contracts and acceptance interpretation

**28 October 2026 runtime update.** The implementation tables below describe the
18 October Arduino/native checkpoint. The active micro T-Kernel build now has
ApplicationTask event/reset/status glue in `runtime_mtk.c`, a typed eight-item
application message buffer, bounded result publication and portable dispatch
tests. See the dated update in [D3_TASKS.md](D3_TASKS.md) and the application
smoke check in [README.md](README.md). Target build/image, queue/resource/core
evidence and real entrance acceptance remain unrun. The reviewed timing,
identity/time-basis and quality-validation findings remain open; descriptions
of frozen semantics below are historical, not a new acceptance claim.

This document records what was planned and executed from the three Cody PRDs. It does not mark physical sensor, crosstalk, WCET or live MQTT evidence complete without a target-board run.

## Delivery plan and result

| Phase | PRD outcome | Implementation | Verification | Status |
|---|---|---|---|---|
| 1 | Preserve a C-facing D1 application boundary | Added portable C modules under `EdgeCore`; no Arduino/FreeRTOS types enter the record, timing or occupancy contracts | Native `-std=c11 -Werror` test; ESP32-S3 build | Complete in code |
| 2 | Freeze D3 timing semantics | Added periodic, one-shot and event-driven state/config contracts with bounded validation and skip-ahead periodic behaviour | Deterministic native timing tests | Complete in code |
| 3 | Integrate logical timing | Scheduler uses the timing state; `TRIGGER` arms one-shot/event-driven work; a completed source sample arms matching `timing.trigger_source` targets | ESP32-S3 compile; hardware trace pending | Code ready, hardware pending |
| 4 | Freeze D6 EventRecord | Added an 80-byte, schema-versioned, fixed-size and pointer-free record | Size and validation tests | Complete in code |
| 5 | Implement ApplicationTask semantics | Typed `app_q`; validation, bounded de-duplication, stale/reorder rejection, rule/ML policy, occupancy floor and non-blocking MQTT request | Native edge-case tests; ESP32-S3 compile | Complete in code |
| 6 | Connect real rule decisions | Added `event:<class>` rule actions and a non-blocking EventRecord sink | Compile and sink test; sensor trial pending | Code ready, hardware pending |
| 7 | Retain evidence | Added host runner and documented CLI/configuration; updated D3 checklist without claiming hardware completion | `sh tools/cody_test.sh`; `sh tools/s3.sh build` | Complete for available environment |

## Frozen D3 contract

| Mode | Arm rule | Execution rule | Completion/re-arm rule |
|---|---|---|---|
| Periodic | `RUN` arms the first execution immediately | Execute at `timing.interval_ms`; if late, skip missed slots rather than burst | Automatically advances from the previous deadline |
| One-shot | `RUN` or `TRIGGER <sensor>` | Execute exactly once | Remains complete until another explicit trigger or a stop/run cycle |
| Event-driven | Matching `timing.trigger_source` sample or `TRIGGER <source>` | Execute once inside the bounded window | A repeated trigger while armed/due is rejected; completion returns to complete until re-triggered |

Configuration fields are `timing.mode`, `timing.interval_ms`, `timing.timeout_ms`, `timing.window_ms`, `timing.trigger_source`, and `timing.enabled`. Periodic intervals are 1–600,000 ms. Timeouts and windows are bounded at 600,000 ms. Event-driven mode requires a non-zero window and a trigger source. A timeout cannot exceed its window. One-shot and event-driven timing require polling mode.

The legacy `freq_hz` field remains supported. If `timing.interval_ms` is not explicit, a valid periodic `freq_hz` is converted into the interval. This preserves the D1 configuration interface while allowing the D3 contract to be explicit.

## Frozen D6 application contract

`event_record_t` contains schema version, event/source/sequence identity, monotonic window timestamps, eight fixed features, quality flags, rule and ML classes, confidence, model version and validity. It has no pointers and is currently 80 bytes.

Application processing order is:

1. Validate schema, identity, validity, timestamps, class bounds, confidence and freshness.
2. Reject a recently committed `event_id` as a duplicate.
3. Reject a non-increasing source sequence or older source timestamp by default.
4. Prefer a valid ML class at or above 0.70 confidence; otherwise use a valid rule class.
5. Apply `entry +1`, `exit max(0, count-1)`, and no change for incomplete/false.
6. Commit the event to a bounded 32-ID RAM de-duplication history.
7. Log the outcome, enqueue a best-effort QoS 1 MQTT application event, and emit a benchmark sample.

Output queue failure is DEGRADED and does not roll back or replay occupancy. Occupancy and de-duplication are intentionally volatile at this stage; reboot persistence is not silently claimed and still needs the team/professor decision recorded in `CODY_WEEK8_SUBMODULE.md`.

## Code map

| Contract | Files |
|---|---|
| Timing types, validation and state transitions | `libraries/EdgeCore/src/application/timing_contract.[ch]` |
| EventRecord schema, validation and sink | `libraries/EdgeCore/src/application/event_record.[ch]` |
| Occupancy/de-dup/reorder/rule-ML policy | `libraries/EdgeCore/src/application/application_state.[ch]` |
| Config fields and validation | `libraries/EdgeCore/src/application/config_parser.c`, `bus/bus_common.h` |
| Scheduler integration and logical source trigger | `libraries/EdgeCore/src/application/scheduler.c`, `output_format.c` |
| Rule-to-event hand-off | `libraries/EdgeCore/src/rule_engine/rule_action.c` |
| Typed queue, ApplicationTask and MQTT output | `libraries/EdgeS3/src/runtime_s3.[h|cpp]` |
| Native acceptance tests | `tests/test_cody_contracts.c`, `tools/cody_test.sh` |

## Verification executed

```text
$ sh tools/s3.sh cody-test
cody contracts: PASS (EventRecord=80 bytes)

$ sh tools/s3.sh build
Sketch uses 1345098 bytes (42%) of program storage space. Maximum is 3145728 bytes.
Global variables use 179220 bytes (54%) of dynamic memory, leaving 148460 bytes for local variables.
```

The native suite covers periodic due/skip-ahead, one-shot exactly once, event trigger idempotence, configuration acceptance/rejection, EventRecord sink copying, entry, exit, occupancy floor, duplicate, incomplete, false, reordered, stale and ML-preferred decisions.

No compatible ESP32-S3 USB device was present during this run, so firmware flashing and live timing/evidence capture were not executed.

## Dependencies that must not be reported as Cody-complete

| Remaining evidence/change | Direct owner | Cody's acceptance responsibility |
|---|---|---|
| Non-blocking HC-SR04 edge/RMT capture | Sitt/Hasif, coordinated with Brendan | Check timeout/quality outcomes and 50 ms margin |
| Two-sonar order, acoustic guard and crosstalk trial | Brendan/Sitt | Accept direction/ambiguity semantics and evidence |
| Continuous LD2450 drain and presence parsing | Sitt/Brendan | Accept freshness/expiry contract |
| Full SchedulerTask/queue/platform measurement | Hasif | Interpret p95/p99/max, misses and resource margin |
| ML event correlation and final class mapping | Jun Yong | Accept deadline, confidence and fallback behaviour |
| Reset persistence, if required | Team decision; Hasif platform support | Freeze replay/reconciliation semantics |
| Real entrance end-to-end trial | Whole team | Decide pass/fail against D6 criteria |

## Commands for the next board session

```sh
sh tools/s3.sh build
sh tools/s3.sh flash
sh tools/s3.sh monitor
```

Then run `APP RESET`, submit the four application classes and duplicate/reorder cases, configure a rule with `action=event:entry`, and capture the `[APP]`, `[BENCH]` and MQTT output. These synthetic commands validate integration plumbing only; the real sensor-to-rule-to-EventRecord path is the evidence required for final D6 acceptance.
