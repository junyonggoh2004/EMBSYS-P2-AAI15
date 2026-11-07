# Use case 1: radar-triggered doorway occupancy counter

Draft: 7 November 2026. Owner: Cody (D3); hardware/capture coordination with
Sitt/Hasif, scheduler and workload coordination with Brendan.

Status: **plan for review; physical execution pending**. This file authorizes
no new wiring, component removal, flash erase or firmware change. It defines
one use case and the evidence required to validate the existing implementation.

## 1. Intended result

A person approaching a doorway is detected by an LD2450 radar. The firmware
opens a short observation window and repeatedly reads two ultrasonic sensors,
A and B, in order. A valid entry increments occupancy once; a valid exit
decrements it once, with a floor of zero. A turnaround, ambiguous observation
or essential sensor failure must not change occupancy.

The initial supported trial is one person at a time through one doorway.
Multiple people are a rejection/fault trial, not a claim of supported counting.
Classification uses the existing rule-based WINDOW controller; no trained ML
model is required for the initial demonstration.

## 2. Recorded baseline and setup gate

Read these before preparing the fixture:

- [Root README: current breadboard](../README.md) and
  [active wiring](../esp32s3_mtk/WIRING.md).
- [Framework README §4 and §5 Parts E/G](MAIN_BRANCH_README.md#4-d3-task-checklist).
- [Architecture](../ARCHITECTURE.md), [setup](../SETUP.md) and
  [interrupt/wiring review](../D3_INTERRUPT_WIRING_REVIEW.md).
- [Single-sonar capture procedure](D3_CAPTURE_V2_TEST.md).

The recorded V2 fixture has one HC-SR04+ on TRIG4/ECHO5, AHT/TSL2561 on
I²C GPIO1/2, and retained LCD wiring on GPIO6–10. It does not establish an
approved second-sonar or radar connection. Older research notes and upstream
test fixtures use different pins; do not apply those layouts to V2.

**BLOCKED: VERIFICATION REQUIRED** until the following record is completed:

| Required evidence | Actual value / evidence reference |
|---|---|
| Board identity, flash and PSRAM | Pending |
| Sonar A exact module/revision, supply and measured Echo levels | Pending |
| Sonar B exact module/revision, supply and measured Echo levels | Pending |
| Each Echo protection/bias circuit and common ground | Pending |
| Radar exact model/revision, supply and UART signal levels | Pending |
| A/B Trigger/Echo and radar TX/RX GPIOs; conflict check | Pending |
| Placement, sensor orientation, doorway dimensions and radar axes | Pending |
| Scope/analyzer model, input limits and measurement resolution | Pending |
| Team approval for the proposed fixture | Pending |

Keep required voltage protection and bias. Internal pulls do not provide voltage
shifting. For every proposed connection/component/interrupt change, record the
baseline README section, existing circuit, proposal, specification evidence,
electrical justification, affected files, regression tests and failure risk.
Follow README review → source verification → exact hardware specification
verification → proposal → team approval → implementation → physical testing →
documentation update. This plan deliberately assigns no additional GPIOs.

## 3. Placement and calibration

1. Mark an outside-to-inside path: A is encountered before B for entry.
2. Arrange and measure the observation regions so a crossing can produce
   **clear → A only → both → B only → clear**. Exit is the reverse. The existing
   classifier requires the intermediate states, including “both”; placement
   that cannot expose these states needs review before counting trials.
3. Arrange the radar zone to detect the approach while both sonars are still
   clear. Confirm its coordinate orientation from observed reports rather than
   assuming which direction is positive.
4. Measure empty-doorway and occupied distances for A and B. Select and record
   occupied thresholds in millimetres from those measurements.
5. Validate each sonar's async capture, missing-Echo error and STOP/CLEAR
   cancellation using the single-sonar procedure before combining them.
6. Measure an acoustic guard with both sonars fitted. Compare isolated readings
   with paired readings in both orders and retain traces/false-width counts.
   The guard and each timeout must support the measured range while satisfying:

   `timeout_A_us + guard_us + timeout_B_us <= 50000`

The V2 preset's two 30 ms timeouts cannot fit this budget. Do not copy arbitrary
shorter values to obtain acceptance: establish range, guard and service margin
with measurements. Passing the configuration budget does not prove completion
within 50 ms under load.

## 4. Existing firmware policy to verify

| Behaviour | Current contract |
|---|---|
| Window opening | First valid radar report with a target inside the configured zone |
| Confirmation | A following in-zone presence report at least 50 ms later |
| Pair order | A, explicit guard, B |
| Nominal pair period | 100 ms |
| Complete pair ceiling | 50 ms, including acquisition and sequencing |
| Window cap | 2 s or 20 attempts; repeated presence does not extend it |
| Clearance | At least three valid clear pairs spanning at least 200 ms |
| Radar expiry | 300 ms without a fresh valid report |
| Repeated failure | Three consecutive failed pairs close degraded |
| Occupancy protection | Essential faults, ambiguity, skipped observations or UART loss prevent count changes |
| Event delivery | One event offer per finished window; inspect drops separately |

A missing echo is an error, not a clear doorway. Do not change the policy to
make a trial pass; record the mismatch and review its cause.

## 5. Run one recorded session

Use the active `esp32s3_mtk` firmware and pinned course SDK/kernel. Preserve the
source commit, dirty diff if any, image hash, build/image audit, board identity,
boot log and the complete approved configuration with every session.

1. Load individually validated configurations named `SONAR_A`, `SONAR_B` and
   `RADAR`, using only the verified pins and voltages. Radar must use raw UART
   at 256000 baud, 8N1. It must not use line mode, pre-write or post-delay.
   Other ultrasonic transmitters must not be configured.
2. Start a raw serial log and ground-truth trial log. Record the initial
   `APP STATUS`. If a zero baseline is needed, use `APP RESET` while stopped
   before the trial, and record the acknowledgement.
3. With sampling stopped, issue `TASKS` to capture initial resources and request
   a SchedulerTask timing-statistics reset. Keep the doorway clear for setup.
4. Enter the following template, substituting measured values. Angle-bracket
   terms are placeholders and must not be pasted literally:

   ```text
   STOP
   WINDOW SONAR_A SONAR_B RADAR <guard_us> <A_mm> <B_mm> <xmin> <xmax> <ymin> <ymax>
   RUN
   ```

   Require `[WINDOW] request=0` and the configuration acknowledgement before
   walking. If boot identity is unavailable or configuration is refused, stop
   and investigate; do not erase NVS as an automatic recovery step.
5. Carry out the trial matrix below. Let the doorway clear and the controller
   rearm between people. Record each physical action and its corresponding
   `[WINDOW]` and `[APP]` lines. Use real crossings, not `APP EVENT` fixtures.
6. After the final trial has closed, record `APP STATUS`, then `STOP`, `TASKS`
   and `WINDOW STATUS`. Preserve any cancellation caused by STOP as a distinct
   outcome. Avoid repeated SHOW/TASKS printing during timed trials.
7. Close the serial monitor, then collect before CLEAR or another WINDOW:

   ```sh
   sh tools/mtk.sh window-report --note "UC1; approved fixture ID; workload ID; trial IDs"
   ```

   Set `PORT` explicitly if automatic detection does not select the intended
   board. The collector saves raw and JSON reports under `parity/logs/mtk/d3/`.
   It reads metrics only; it does not configure, RUN or STOP the board.

STOP disables WINDOW. A new WINDOW command is required before the next session,
and that configuration resets its cumulative metrics. Save the report first.

## 6. Ground-truth trials

Use the [trial worksheet](D3_TRIAL_WORKSHEET.md) for session provenance,
pre-filled trial rows, timing/resource results and evidence references.

Suggested pilot: ten entries and ten exits, interleaved, plus three repeats of
each negative case. These are proposed collection counts, not an agreed final
accuracy qualification. Record all trials, including failures and retries.

| Trial | Physical action | Expected result |
|---|---|---|
| Entry | Walk from outside through A then B | One accepted entry; occupancy +1 |
| Exit | After a real entry, walk back through B then A | One accepted exit; occupancy −1 |
| Approach only | Enter the radar zone, remain clear of sonars, leave | No occupancy change |
| Turnaround | Reach A, turn back before completing the crossing | No occupancy change |
| Reverse turnaround | Reach B from inside, then turn back | No occupancy change |
| Dwell / overlong crossing | Remain in the observation area beyond the window | No late accepted crossing from the expired window |
| Multiple people | Two people overlap in the monitored zone | Ambiguity must prevent an unsupported count change |
| Essential sensor fault | Use a separately approved missing-Echo/radar-loss setup | Explicit fault/stale outcome; no occupancy change |

Repeat entry/exit with different walking speeds and subjects after the initial
pilot. A trial outside the observable state sequence is a recorded limitation,
not a reason to discard ground truth. Fault setup requires power-off changes
and its own verified circuit; do not disconnect powered wires for this matrix.

Use one row per trial:

| Trial ID | Workload | Ground truth | Window sequence ID(s) | Window class/reason | APP outcome | Count before/after | Correct? | Evidence / notes |
|---|---|---|---|---|---|---|---|---|
| Pending | | | | | | | | |

Compare event classifications and applied occupancy separately. `queued=1`
means the event was offered successfully; APP `publish=1` means publication was
enqueued, not acknowledged by the broker. Offline publication failure does not
roll back an already applied occupancy transition.

## 7. Measure the actual entrance workload

Repeat the same physical trials in separate sessions with unchanged geometry
and calibration:

| Workload | Enabled activity |
|---|---|
| W1 | Radar + two-sonar WINDOW, minimal other activity |
| W2 | W1 + the existing AHT/TSL2561 sampling on the verified fixture |
| W3 | W2 + normal Wi-Fi/MQTT traffic |
| W4 | W3 + the actual intended inference/rules workload, once configured and verified |

Record actual rates, model/rules, network conditions and duration. If a workload
is unavailable, mark it not run. D2 application flash logging remains a separate
integration gate; do not add SPI/flash tests to the retained LCD fixture.

| Measurement | Evidence / interpretation |
|---|---|
| Pair attempts and outcomes | WINDOW report: pairs, valid, failed, cancelled |
| Pair latency | min/mean/max and p95/p99 upper bounds from WINDOW report |
| Deadline and scheduling loss | deadline_misses and skipped counters |
| Radar/service faults | Frames, malformed frames, UART loss since configure, radar-stale/window-fault counts |
| Event integrity | Window classifications, event drops, APP results and ground-truth table |
| Scheduler work per wake-up | TASKS cycles, mean, bucketed p95 and observed maximum |
| Resources | TASKS stack, heap, core residency and message-drop counters |
| Physical timing/crosstalk | Common-timebase A/B Trigger and Echo traces; record scope resolution and how traces align with trial IDs |

WINDOW percentiles use 1 ms buckets and include cooldown, failed and cancelled
attempts. TASKS cycle p95 uses different buckets and has no cycle p99 report.
Keep these metrics distinct: SchedulerTask work per wake-up is not complete-pair
latency or the duration of an entrance window. Measured maxima are observations,
not a proven worst-case execution bound.

Do not run `sh tools/mtk.sh wcet` or `timing` against this configured fixture:
the existing scripts CLEAR/reconfigure sensors on other fixture pins and do not
measure this real WINDOW workload. Use the session procedure above.

## 8. Acceptance and handoff

- All planned normal pilot crossings match ground truth, change occupancy once
  in the correct direction and leave the expected final count. Report the
  numerator/denominator and every failure; do not infer general accuracy from
  the pilot alone.
- Negative/fault trials never produce an unsupported occupancy change.
- Recorded pair maxima are at most 50000 µs, with zero deadline misses and
  skipped slots in the normal qualification workloads. Retain and explain
  fault/cancellation timings rather than omitting them from the report.
- No unexplained UART loss, malformed frames, event/message drops, reset or
  capture resource leak. Repeated sessions recover and rearm correctly.
- TASKS confirms the documented task placement, including ApplicationTask core
  1/priority 23, and reports stack/heap evidence. Agree numeric headroom and
  SchedulerTask cycle limits with Hasif/Brendan before declaring resource margin.
- Physical traces establish order, guard, full-pair timing and absence of
  observed crosstalk for the tested geometry and workload.

Deliver raw serial logs, ground-truth rows, WINDOW JSON/raw reports, TASKS
captures, physical traces and configuration/build provenance. Record unresolved
accuracy, resource or hardware limitations with an owner and disposition.
After team review and successful physical validation, update the README,
architecture/pin diagrams and test instructions with the verified fixture.
Keep D3-2 through D3-10 physical gates open until their own evidence is complete;
this use-case plan or a successful build does not close them.
