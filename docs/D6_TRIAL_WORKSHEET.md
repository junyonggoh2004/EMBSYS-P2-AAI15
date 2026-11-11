# D6 three-board session worksheet

Template prepared 10 November 2026. **NOT RUN**. Use with the
[validation plan](D6_USECASE_VALIDATION_PLAN.md). Copy once per session; a blank
field is unknown, never zero/pass. Record failures and retries separately.
Refined 11 November 2026 with decision and per-attempt evidence fields; see
the [next integration steps](d6/NEXT_STEPS.md).

## Decision register

Record the agreed value and owner/date/evidence before running dependent
trials. PENDING does not mean an interface or numerical limit is approved.

| Decision | Agreed value / owner / date / evidence | Dependent trials |
|---|---|---|
| Scenario, doorway inclusion, motion source and output behavior | PENDING | T01, T03, T16 |
| A/B/C inventory, unique node/source names, pin/power diagrams | PENDING | All physical trials |
| C panel geometry, offsets, orientation, colors and fixture | PENDING | All display trials |
| Source/profile/field mapping, units and display precision | PENDING | T01, T02, T10 |
| Freshness, refresh limits and disconnect/reset behavior | PENDING | T11–T15 |
| SPI devices, supported operations and approved write location | PENDING | T04, T05 |
| ML model/input/output meaning and result transport | PENDING | T06–T09, T15 |
| Live model/source activation and failure recovery semantics | PENDING | T07–T09 |
| Workload, repetitions/duration and resource/timing limits | PENDING | All measured trials, T16 |

## Session and readiness

| Field | Actual value / evidence |
|---|---|
| Session ID, date/time/timezone, operators | Pending |
| Scenario approval, doorway included/excluded | Pending |
| G0–G7 prerequisites ready; blocked stages | Pending |
| Source commit, dirty diff, integrated main/LCD revisions | Pending |
| SDK/kernel, per-board image hash and flash/boot capture | Pending |
| A/B/C actual node IDs, board types, serial ports | Pending |
| Per-board device/pin/power diagram and fixture revision | Pending |
| Broker address/network description (no passwords) | Pending |
| Complete presets and per-command acknowledgements | Pending |
| Motion source, rule/output semantics, thresholds/polarity | Pending |
| Sample rates, freshness/skew, display precision/refresh limits | Pending |
| Model hash/ID, input order/units/preprocessing, output meaning | Pending |
| ML result/status topic and agreed payload version | Pending |
| SPI devices, protocols, disposable write location if needed | Pending |
| Workload W1/W2/W3/W4 and planned trial repetitions/duration | Pending |
| Agreed latency/drop/stack/heap limits and reviewer | Pending |
| Raw serial/MQTT and display recording filenames | Pending |

## Trial results

Fill observed behavior, identifiers, timings and evidence; use PASS, FAIL,
INCONCLUSIVE, BLOCKED or NOT RUN explicitly. Add one row per attempt.

| ID | Trial | Status | Actual observation and evidence |
|---|---|---|---|
| T01 | A activity/light → rule/output → MQTT → LCD | NOT RUN | Pending |
| T02 | B temperature/humidity → MQTT → LCD | NOT RUN | Pending |
| T03 | Complete and piecewise configuration, same image | NOT RUN | Pending |
| T04 | GPIO/I2C/SPI concurrent fixture, byte round trip | NOT RUN | Pending |
| T05 | SPI device substitution and reconfiguration | NOT RUN | Pending |
| T06 | Local ML → subscriber → labelled LCD result | NOT RUN | Pending |
| T07 | Live network model change with continued sampling | NOT RUN | Pending |
| T08 | Local → remote → local ML input change | NOT RUN | Pending |
| T09 | Invalid setup/model preserves previous valid state | NOT RUN | Pending |
| T10 | Invalid/truncated/wrong-source frame rejected | NOT RUN | Pending |
| T11 | Stopped sensor becomes stale; others continue | NOT RUN | Pending |
| T12 | Broker outage/reconnect | NOT RUN | Pending |
| T13 | Source reset, reconfiguration and recovery | NOT RUN | Pending |
| T14 | Display reset, re-subscribe and fresh data | NOT RUN | Pending |
| T15 | Missing ML input produces explicit invalid result | NOT RUN | Pending |
| T16 | Combined-load run and resource capture | NOT RUN | Pending |

## Per-attempt evidence (copy for each attempt)

Keep retries separate. Use the trial ID above and a unique attempt ID.

| Field | Actual value / evidence |
|---|---|
| Trial ID, attempt ID, operator and host/video time | Pending |
| Preconditions: fixture/config revision, RUN state and decision references | Pending |
| Exact stimulus/command or injected frame; expected behavior/limit | Pending |
| Source serial record and raw topic/payload; subscriber capture position | Pending |
| Decoded value/unit/status and photo/video position on C | Pending |
| Correlation method; measured delay and uncertainty if timing is assessed | Pending |
| TASKS/error/drop counts before and after when relevant | Pending |
| Actual observation; PASS/FAIL/INCONCLUSIVE/BLOCKED/NOT RUN and reason | Pending |
| Recovery performed, restored configuration and next attempt reference | Pending |

For T10, include length mismatch/truncation, invalid hex and wrong source;
confirm rejected frames neither replace a valid reading nor refresh its age.
For T11, record the last valid receive time, agreed age limit and stale display
time while another source continues. For T12–T14, capture startup/reconnect
and the first fresh displayed sample separately from presence/heartbeat.

## End-to-end correlation and resources

For each stimulus/model transition record source node/sensor, actual payload,
generation where supported, decoded value/unit, subscriber record and screen
photo/video position. Use a common host/video timeline or an agreed trace
method; independent board uptimes are not a synchronized clock.

| Measurement | Actual / evidence / interpretation |
|---|---|
| Stimulus → local output → subscriber → screen timeline | Pending |
| Requested vs measured sample intervals and gaps | Pending |
| Source failure → stale indication; reconnect → fresh display | Pending |
| Model transition, sample continuity and result generation | Pending |
| TASKS placement, per-task stack headroom, heap | Pending |
| Queue/error/drop counts before/after; resets | Pending |
| Display geometry/colors/units and source association | Pending |
| SPI intended/read byte, device/config identity | Pending |
| Actual workload duration and config changes | Pending |

## Review and remaining gates

| Summary | Record |
|---|---|
| Planned / attempted / not run / blocked | Pending |
| Passed / failed / inconclusive; retry references | Pending |
| Open issues, owner and next action | Pending |
| Evidence directory and reviewer/date | Pending |
| D6 task rows eligible for status update | Pending |

No successful build, heartbeat, queue enqueue or LCD test pattern alone closes
the end-to-end path. Do not count BLOCKED/NOT RUN as passed. D3 doorway timing,
D2 application logging/T-23 and meaningful anomaly detection each retain their
own acceptance gates.
