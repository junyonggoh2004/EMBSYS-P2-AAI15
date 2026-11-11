# D6 next integration steps

Reviewed 11 November 2026 against `d6` at `3e029d0`. This is an implementation
handoff; no D6 hardware trial or team decision is marked complete.
Use the [plan](../D6_USECASE_VALIDATION_PLAN.md) and copy the
[worksheet](../D6_TRIAL_WORKSHEET.md) for each actual session.

## Baseline checked

| Item | Finding at this review |
|---|---|
| Main integration | Merge `3e029d0` includes main checkpoint `b389714`; initial working tree clean |
| LCD reference | Local remote-tracking ref `origin/lcd-output` resolves to `b1c1675`; reviewed without fetching or importing |
| LCD capability | Single-caller synchronous drawing API; opt-in `LCDTEST` through BenchmarkTask; no remote sensor display adapter |
| Sensor network | `/line` publisher exists; `/raw` publication returns false on the active MTK backend |
| Measurement decoding | `sensor_measurement_value()` exposes typed field decoding; physical-unit JSON is supplementary console output |
| ML integration | Current ML configuration changes require sampling stopped; remote bindings and result publication remain separate D4 work |

These are local source/reference observations. The LCD branch records its own
visible test-pattern evidence; that does not prove this combined D6 firmware.

## Step 1 — record the fixture and interface decisions

Cody leads with the owners named in plan section 4. Complete the worksheet's
decision register before turning proposed node names and pins into presets.

1. Confirm the scenario and doorway inclusion, actual motion source and local
   output. Record output behavior, polarity and failure state.
2. Record one pin/power diagram per board and unique node/source names. Treat
   the existing three-sensor breadboard and Brendan's LCD-only fixture as
   separate layouts until a checked diagram reconciles them.
3. Agree the display's source-to-field mappings, units, age/refresh limits and
   behavior on disconnect/reset. Agree the ML input/model/result contract with
   the D4 owners; reserve a display field without inventing a result topic.
4. Obtain Brendan's two SPI device protocols and supported configuration
   operations before preparing task 5.4's commands.

**Deliverable:** completed decision rows with owner/date/evidence, plus fixture
diagrams and per-board configuration requirements. Unanswered rows stay
PENDING and block their dependent board trials. Source review and native work
that do not require those answers can continue.

## Step 2 — integrate the LCD component against this baseline

Brendan leads with Cody reviewing the integration. Compare against the exact
reviewed LCD commit; if the reference changes, record and review the new diff.

| Scope to review | Integration requirement |
|---|---|
| `components/lcd_output/` under `esp32s3_mtk/` | Import driver, headers, host tests and fixture/evidence documentation |
| Component/main CMake files | Add dependencies while retaining current sensor/ML sources and build settings |
| `runtime_mtk.c` and `include/runtime_mtk.h` | Apply typed diagnostic handoff to current code; retain application, inference, SPI and benchmark dispatch |
| `tests/test_mtk_application_runtime.c`, `tools/mtk_runtime_test.sh` | Combine diagnostic coverage with current application tests |
| README, WIRING and validation notes | Retain both fixture histories and their evidence limits |

Record actual `sizeof(bench_msg_t)`, queue capacity and internal-RAM change;
do not estimate message-buffer growth from the added fields alone. Keep the
panel inactive until an explicit verified configuration is supplied. The
reference command `LCDTEST 76 284 82 18 00 0` describes Brendan's tested module;
confirm it against the actual C panel and check all four edges and colors.

**Exit gate:** LCD host test and existing native regressions pass; pinned-SDK
build/image audit passes; the intended panel visibly renders correctly on the
combined firmware, with TASKS/resources captured. Record software and board
results separately. LCD import alone leaves task 5.5 pending live-data evidence.

## Step 3 — implement and validate the remote sensor display path

Start with T02, B's AHT temperature/humidity → MQTT → C display. Add A's light
and activity path after their fixture/behavior decisions. ML display follows
the agreed D4 result transport rather than blocking this first sensor path.

The existing wire envelope is:

```text
topic:   pico/<node>/sensor/<source>/line
payload: proto=<protocol> src=<source> len=<byte-count> ts=<source-uptime-ms> : <hex bytes>
```

`application/output_format.c` constructs this text in a bounded 256-byte
buffer. Long frames can therefore be truncated while retaining the declared
length. The display adapter must reject length disagreement. In
`mqtt/sensor_bridge.c`, the existing parser extracts bytes after `:` using
`strtoul()` without complete token validation and caps its local buffer at
64 bytes. Reusing it alone does not satisfy T10's malformed-frame gate.

Implement the following once the mappings are agreed:

1. Validate exact topic structure and configured node/source, envelope source,
   protocol, declared length and every hex token. Reject truncation, excess
   bytes, malformed fields and unsupported profiles before changing state.
2. Keep parsing, typed decoding and display-state logic in EdgeCore. Reuse
   `sensor_measurement_value()` with an explicit configured profile; an MQTT
   source name alone does not identify an AHT or TSL2561 decoder.
3. Send bounded copied data through a checked nonblocking handoff. Assign one
   task to display state/drawing; route `LCDTEST` through that owner or reject
   it while live display owns the driver. Budget queue, state, stack and RAM.
4. Track local receive age and distinguish waiting, valid, stale and error
   states according to the agreed contract. A rejected frame must not refresh
   the last valid reading. Never compare uptimes across boards as one clock.
5. Add native tests for decoding, source mismatch, malformed/truncated frames,
   no state update on rejection, age expiry, reconnect/reset handling and full
   handoff/drop reporting. Include valid zero and a rejected frame after a
   valid sample so missing-data behavior cannot pass by substituting zero.

**Exit gate:** T02, T10 and T11 have correlated source bytes, subscriber logs
and visible display evidence; T12–T14 separately prove recovery. Native tests
establish parser/state behavior, not screen output or scheduling acceptance.

## Verification and handoff

For implementation changes run the focused tests, `sh tools/cody_test.sh`,
`sh tools/d2_host_test.sh`, `sh tools/check_hal_boundary.sh`,
`sh tools/mtk.sh purity` and `git diff --check`. Add the LCD host test when
imported, then pinned-SDK build/image and relevant board tests from the plan.

Use `parity/logs/mtk/d6/<session-id>/` for actual session evidence. Populate
each trial's precondition, expected behavior, observation, status and artifact
references. Keep blocked/unrun trials and unresolved limits visible. SPI
configuration, ML publication/live changes and full rehearsal remain later
gates; these three steps do not close tasks 5.1–5.8 by themselves.
