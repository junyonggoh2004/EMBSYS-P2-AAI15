> Archived doorway reference, copied from `origin/main` at `b389714` on 10 November 2026.
> Retained as historical planning material; NOT RUN. Relative links below belong
> to the original `docs/` location and upstream tree. Use
> [the current D6 plan](../../D6_USECASE_VALIDATION_PLAN.md) for this branch.

# D6 use case 1: physical trial worksheet

Deliverable: D6, the end-to-end use case. Prepared: 8 November 2026, by Cody.
Template status: **NOT RUN**.

Use with the [D6 doorway occupancy validation plan](D6_USECASE_VALIDATION_PLAN.md).
Copy this worksheet for each workload/session and replace placeholders with
observations. It is a data-entry template, not physical acceptance evidence.
All rows start NOT RUN; a blank field or dash never means zero or pass.

## 1. Session and readiness

| Field | Record |
|---|---|
| Session ID | UC1-YYYYMMDD-Wx-Sxx |
| Date/time and timezone | — |
| Operator / ground-truth observer | — |
| Workload | W1 / W2 / W3 / W4 |
| Actual session start/end and duration | — |
| Board identity / serial port | — |
| Source commit / dirty diff reference | — |
| Flashed image SHA-256 / flash and boot evidence | — |
| Pinned SDK/kernel build and image-audit evidence | — |
| Verified fixture ID / approval reference | — |
| Exact A/B sonar and radar models | — |
| Verified supply/signal voltages and protection/bias evidence | — |
| Actual A/B Trigger/Echo and radar TX/RX pins | — |
| Geometry/photo/sketch and radar-axis reference | — |
| Full sensor configuration file and WINDOW command | — |
| A/B occupied thresholds (mm) | — |
| A/B capture timeouts and guard (µs) | — |
| Radar zone xmin/xmax/ymin/ymax (mm) | — |
| Peer sampling rates / rules / model / inference rate | — |
| Wi-Fi/MQTT connection, traffic rate and broker-log reference | — |
| Instruments, input limits, resolution and trace clock alignment | — |
| Initial APP STATUS / expected physical occupancy baseline | — |
| Any APP RESET before trials and acknowledgement reference | — |
| Initial TASKS capture / timing-reset request reference | — |
| Raw serial log / ground-truth video or observer notes | — |

Before recording trials, confirm:

- [ ] Exact fixture, supply/signal levels, protection and pins verified and approved.
- [ ] Individual async capture, missing Echo and pending STOP/CLEAR checks have evidence.
- [ ] Geometry exposes clear → A → both → B → clear, and the reverse.
- [ ] A/B guard and useful-range timeouts measured; their sum is at most 50000 µs.
- [ ] Boot identity available; WINDOW configuration acknowledged successfully.
- [ ] Logging and independent ground-truth observation are active.
- [ ] Doorway clear, controller ready, starting count recorded.

If electrical or fixture evidence is missing, record **not ready** and leave
the affected trials NOT RUN.
No new GPIO, guard, threshold or fault circuit is selected by this worksheet.

## 2. Trial recording rules

Use full IDs such as `UC1-20261108-W1-S01-N01` to distinguish identical trial
numbers across sessions. The date in that example is a naming example only.

For every row:

1. Record the actual physical action, subject code, speed and start/end time
   in the detail record. Keep observer ground truth independent of firmware.
2. Record every associated window ID and APP result, including extra events.
   Use node/boot/sequence where available; sequence alone can repeat after boot.
3. Record firmware counts before/after and compare their change with ground
   truth. Check final absolute count against the independent running total.
   Do not redefine expected count from an already incorrect firmware count.
4. Mark PASS, FAIL, INCONCLUSIVE or NOT RUN. Missing evidence is INCONCLUSIVE.
   An unsupported count change is FAIL even if the next trial corrects it.
5. Link evidence using filename plus log line, timestamp or trace interval.
   Keep failed attempts; assign retries new IDs such as N01-R1.
6. Let the doorway clear and WINDOW rearm between actions. Do not reset counts
   between trials to hide drift. An inside-start trial needs a recorded subject
   setup; any setup crossing must also be logged.

The pilot below proposes ten entries, ten exits and three repeats of eight
negative/fault cases. These collection counts are not a final statistical
accuracy qualification. Split fault fixtures into separately identified,
approved sessions as required; leave unrelated rows NOT RUN in those copies.

## 3. Normal crossings: 20 planned trials

Expected entry sequence: clear → A only → both → B only → clear.
Expected exit sequence: clear → B only → both → A only → clear.
Start with a recorded zero baseline if using the alternating sequence below.

“Window / APP” must include class/reason, accepted/rejected result and IDs.
“Count before → after” is the observed firmware count; expected delta is
pre-filled, not an observed result.

| Trial | Planned action | Expected Δ | Actual action / time | Window / APP | Count before → after | Evidence reference | Verdict | Notes |
|---|---|---:|---|---|---|---|---|---|
| N01 | Entry: outside → A → B → inside | +1 | — | — | — | — | NOT RUN | — |
| N02 | Exit: inside → B → A → outside | −1 | — | — | — | — | NOT RUN | — |
| N03 | Entry: outside → A → B → inside | +1 | — | — | — | — | NOT RUN | — |
| N04 | Exit: inside → B → A → outside | −1 | — | — | — | — | NOT RUN | — |
| N05 | Entry: outside → A → B → inside | +1 | — | — | — | — | NOT RUN | — |
| N06 | Exit: inside → B → A → outside | −1 | — | — | — | — | NOT RUN | — |
| N07 | Entry: outside → A → B → inside | +1 | — | — | — | — | NOT RUN | — |
| N08 | Exit: inside → B → A → outside | −1 | — | — | — | — | NOT RUN | — |
| N09 | Entry: outside → A → B → inside | +1 | — | — | — | — | NOT RUN | — |
| N10 | Exit: inside → B → A → outside | −1 | — | — | — | — | NOT RUN | — |
| N11 | Entry: outside → A → B → inside | +1 | — | — | — | — | NOT RUN | — |
| N12 | Exit: inside → B → A → outside | −1 | — | — | — | — | NOT RUN | — |
| N13 | Entry: outside → A → B → inside | +1 | — | — | — | — | NOT RUN | — |
| N14 | Exit: inside → B → A → outside | −1 | — | — | — | — | NOT RUN | — |
| N15 | Entry: outside → A → B → inside | +1 | — | — | — | — | NOT RUN | — |
| N16 | Exit: inside → B → A → outside | −1 | — | — | — | — | NOT RUN | — |
| N17 | Entry: outside → A → B → inside | +1 | — | — | — | — | NOT RUN | — |
| N18 | Exit: inside → B → A → outside | −1 | — | — | — | — | NOT RUN | — |
| N19 | Entry: outside → A → B → inside | +1 | — | — | — | — | NOT RUN | — |
| N20 | Exit: inside → B → A → outside | −1 | — | — | — | — | NOT RUN | — |

## 4. Negative and fault trials: 24 planned trials

Each trial expects **no occupancy change**. Approach-only may produce a false
event; turnarounds and degraded observations need not share one reason code.
Record the actual class and reason, then assess it against the validation plan.

Dwell and multiple-person trials must have explicit ground truth. If someone
actually crosses during an ambiguous case, record that fact separately from
the protective policy's expected zero firmware delta.

Missing-Echo/radar-loss trials require separately approved, power-off fixture
preparation. Do not disconnect powered leads. After restoring the verified
fixture, record recovery and a fresh normal entry/exit check with new trial IDs.

| Trial | Planned action | Expected Δ | Actual action / time | Window / APP | Count before → after | Evidence reference | Verdict | Notes |
|---|---|---:|---|---|---|---|---|---|
| AP1 | Radar approach only; sonars remain clear | 0 | — | — | — | — | NOT RUN | — |
| AP2 | Radar approach only; sonars remain clear | 0 | — | — | — | — | NOT RUN | — |
| AP3 | Radar approach only; sonars remain clear | 0 | — | — | — | — | NOT RUN | — |
| TA1 | Outside turnaround after reaching A | 0 | — | — | — | — | NOT RUN | — |
| TA2 | Outside turnaround after reaching A | 0 | — | — | — | — | NOT RUN | — |
| TA3 | Outside turnaround after reaching A | 0 | — | — | — | — | NOT RUN | — |
| TB1 | Inside turnaround after reaching B | 0 | — | — | — | — | NOT RUN | — |
| TB2 | Inside turnaround after reaching B | 0 | — | — | — | — | NOT RUN | — |
| TB3 | Inside turnaround after reaching B | 0 | — | — | — | — | NOT RUN | — |
| DW1 | Dwell beyond the window; no completed crossing | 0 | — | — | — | — | NOT RUN | — |
| DW2 | Dwell beyond the window; no completed crossing | 0 | — | — | — | — | NOT RUN | — |
| DW3 | Dwell beyond the window; no completed crossing | 0 | — | — | — | — | NOT RUN | — |
| MP1 | Two people overlapping in the zone | 0 | — | — | — | — | NOT RUN | — |
| MP2 | Two people overlapping in the zone | 0 | — | — | — | — | NOT RUN | — |
| MP3 | Two people overlapping in the zone | 0 | — | — | — | — | NOT RUN | — |
| FA1 | Approved missing-Echo A fixture | 0 | — | — | — | — | NOT RUN | — |
| FA2 | Approved missing-Echo A fixture | 0 | — | — | — | — | NOT RUN | — |
| FA3 | Approved missing-Echo A fixture | 0 | — | — | — | — | NOT RUN | — |
| FB1 | Approved missing-Echo B fixture | 0 | — | — | — | — | NOT RUN | — |
| FB2 | Approved missing-Echo B fixture | 0 | — | — | — | — | NOT RUN | — |
| FB3 | Approved missing-Echo B fixture | 0 | — | — | — | — | NOT RUN | — |
| FR1 | Approved radar-loss fixture | 0 | — | — | — | — | NOT RUN | — |
| FR2 | Approved radar-loss fixture | 0 | — | — | — | — | NOT RUN | — |
| FR3 | Approved radar-loss fixture | 0 | — | — | — | — | NOT RUN | — |

## 5. Per-trial detail record

Repeat this block whenever the table cannot hold the evidence. Do not replace
the planned row or omit extra windows.

| Field | Record |
|---|---|
| Full trial ID / retry-of ID | — |
| Subject code(s), direction, walking speed description | — |
| Ground-truth start/end and actual action | — |
| Expected physical count before/after | — |
| Observed sonar-state sequence / supporting trace | — |
| Radar presence/confirmation/loss evidence | — |
| All window node/boot/sequence IDs | — |
| WINDOW class, reason, pairs/valid/failed and queued result | — |
| All APP results, reasons, count transitions and publication status | — |
| Extra/missing events or unexplained count changes | — |
| Raw-log lines, trace interval and observer/video reference | — |
| Verdict and rationale | — |
| Failure owner / issue / disposition | — |

A WINDOW `queued=1` is an event handoff. APP `publish=1` is MQTT enqueue,
not broker acknowledgement. Record broker receipt separately when tested.
Neither successful publication nor an apparently correct final count proves
that each trial was counted correctly.

## 6. Session timing and resource results

After the final trial closes: record APP STATUS, STOP, TASKS and WINDOW STATUS.
Record any STOP-cancelled attempt separately. Close the serial monitor and
collect the nonce-framed report before CLEAR or another WINDOW configuration:

```sh
sh tools/mtk.sh window-report --note "<session ID>; <fixture ID>; <trial IDs>"
```

Replace placeholders with actual values. Keep the generated raw and JSON
filenames. STOP disables WINDOW; reconfiguration for another session resets
its metrics. A partial capture must be preserved and labelled partial.

| Measurement | Observed value | Evidence reference / interpretation |
|---|---|---|
| WINDOW report raw / JSON filenames | — | — |
| Attempt counts: pairs / valid / failed / cancelled | — | — |
| Pair min / mean / max (µs) | — | — |
| Pair p95 / p99 upper bounds (µs) | — | 1 ms buckets; retain overflow |
| Pair deadline misses / skipped slots | — | — |
| Entry / exit / incomplete / false window totals | — | — |
| Window deadlines / radar-stale / window-fault totals | — | — |
| Event drops / UART loss since configuration / malformed frames | — | — |
| SchedulerTask cycles / mean / p95 bound / max (µs) | — | TASKS; no cycle p99 provided |
| ApplicationTask core / priority / core residency | — | Expected core 1, priority 23 |
| Per-task stack headroom / heap / drop counters | — | Attach complete TASKS output |
| Scope/analyzer pair maximum and measured guard (µs) | — | Define trace measurement endpoints |
| False/extra/missing edges or crosstalk observations | — | — |
| Final firmware count / independently expected count | — | — |
| Reset, recovery or configuration changes during session | — | — |

Check `pairs = valid + failed + cancelled`. Keep WINDOW durations, SchedulerTask
work per wake-up and physical trace measurements separate. WINDOW timing
includes cooldown, failures and cancellations; it is not a per-person-only
distribution. Do not subtract bucket percentiles to infer a trial percentile.

Do not run the existing `wcet` or `timing` scripts on this fixture: they
CLEAR/reconfigure other GPIO layouts and do not preserve the WINDOW workload.

## 7. Results and review

| Summary | Result |
|---|---|
| Planned / attempted / NOT RUN trials | — |
| PASS / FAIL / INCONCLUSIVE trials | — |
| Entries correct / attempted | — |
| Exits correct / attempted | — |
| Negative/fault trials with unsupported count changes / attempted | — |
| Missing / duplicate accepted crossings | — |
| Final occupancy error: firmware minus ground truth | — |
| Normal-workload pair max ≤50000 µs, zero misses/skips? | — |
| Unexplained faults, UART loss, queue drops or resets? | — |
| Physical order/guard/crosstalk evidence sufficient? | — |
| Resource evidence reviewed; agreed limits and reviewer | — |
| Session disposition / unresolved issues and owners | — |
| Reviewer / review date / evidence archive location | — |

Report first attempts and retries separately; show every failed or inconclusive
trial in the totals. Do not count NOT RUN rows as successful tests. Any unavailable
workload or missing physical evidence remains open.

Use W1 = radar + two sonars; W2 = W1 + AHT/TSL2561; W3 = W2 + normal Wi-Fi/MQTT;
W4 = W3 + verified intended inference/rules. Repeat with unchanged calibration
and geometry, or assign a new fixture/configuration revision.

This worksheet cannot close hardware, timing or merge acceptance until actual
measurements are entered and reviewed against the use-case plan.

