# D6: three-board end-to-end validation plan

Prepared 10 November 2026 on branch `d6`. Status: **planning only; NOT RUN**.
Lead: Cody. Brendan implements the SPI-config demonstration and remote-data LCD
path; Hasif/Sitt integrate the D4 network/configuration path; Jun Yong supplies
the verified model/input/output contract. These follow the task split recorded
in `origin/main` README section 7 at `b389714`.

## 1. Objective and scope

Demonstrate one configurable edge system: acquire physical sensor data, process
it through rules and ML, send it over MQTT and show it on a separate LCD board.
Change supported configuration on the same firmware image and show the effect.
Also demonstrate GPIO, I2C and SPI together, including a configured SPI byte
write/read and a device substitution after safe power-off and reconfiguration.

The proposed application is a room/activity and environmental monitor. This is
a concrete implementation proposal for the three-board direction, not a claim
that task 5.1 has team sign-off. D3 supplies timing and sequencing; D4 supplies
ML. Neither the whole repository nor D6 is a D3-only project.

The original doorway plan's readiness checks, controlled workloads, failure
trials, provenance and evidence discipline are reused here. Its occupancy
classification and 50 ms two-sonar acceptance are conditional on retaining the
doorway extension, not automatic acceptance criteria for this three-board demo.
The original [plan](d6/reference/D6_USECASE_VALIDATION_PLAN.md) and
[worksheet](d6/reference/D6_TRIAL_WORKSHEET.md) are archived without claiming
their tests ran. Use the [current worksheet](D6_TRIAL_WORKSHEET.md) for new runs.

## 2. Branch readiness: first integration gate

The new branch starts at `1c30eeb` (the `d3-board` checkpoint), not the latest
project baseline. `origin/main` at `b389714`, containing the merged MQTT and
first ML checkpoints, is now integrated in the prepared merge. Conflicts in
README, feedback history and the two D6 documents were reconciled: the current
three-board plan is retained here and the upstream doorway documents remain
archived references. Automatic merging of runtime files is not behavioral
verification; combined validation and LCD integration retain separate gates.

1. Preserve the existing README edit, untracked D3 hardware guide and other
   local files. Review the exact diff before integrating anything.
2. Review the prepared `main` integration into `d6`;
   resolve feedback/document history without dropping retained D3 fixes or
   newer MQTT/ML behavior. Do not replace main's README with the older D3 copy.
3. Review Brendan's `lcd-output` component against that combined baseline.
   Bring over the driver/typed handoff and relevant tests, not an older runtime
   wholesale. Keep LCD geometry, offsets and pins specific to the verified panel.
4. Build and audit the combined target, then run regressions before wiring the
   multi-board system. Record the exact revisions actually integrated.

The prepared merge integrates main only; it does not integrate `lcd-output`,
flash boards or implement the missing adapters. `D3_TASKS.md` and `CODY_WEEK8_SUBMODULE.md` are
absent from this checkpoint; use the current upstream README task list rather
than claiming those missing local handoffs were reviewed.

## 3. Board responsibilities and visible result

Node IDs below are proposed deployment names; record actual names before runs.
They do not redefine canonical event identity or cross-node time contracts.

| Board | Proposed node | Input and responsibility | Visible evidence |
|---|---|---|---|
| A: activity/light | `s3-001` | TSL2561 plus a team-selected motion/presence source; local rule drives a verified output; designated GPIO/I2C/SPI demonstration fixture | Physical stimulus changes raw reading, rule/output and the corresponding LCD field |
| B: environment/ML | `s3-002` | AHT temperature/humidity; run a supported model with explicitly configured local bindings first | Physical readings and successful model input/output; later publish the result |
| C: display | `s3-003` | Subscribe to A/B data, decode measurements, maintain bounded display state and draw through Brendan's LCD driver | Activity, lux, temperature, humidity, ML model/result and per-source validity |
| Laptop | Broker and evidence | Mosquitto, command publication, raw MQTT/serial capture | Independent evidence that messages reach a subscriber |

```mermaid
flowchart LR
    A["A: activity + light → rule/output"] -->|sensor readings| M[Mosquitto]
    B["B: temperature/humidity → ML"] -->|sensor readings + ML result| M
    M -->|subscribed data| C["C: decode → latest valid state → LCD"]
    L[Laptop configuration and capture] --> M
    M -->|config and commands| A
    M -->|config and commands| B
    M -->|display configuration: pending interface| C
```

Do not label ultrasonic distance as motion or occupancy without specifying and
testing the decision rule. Select the real motion source, output device, pins,
thresholds and polarity with Sitt/Brendan before writing runnable presets.
The doorway alternative needs an approved second sonar, radar, geometry and
its own acceptance; the existing one-sonar fixture does not provide that proof.

## 4. Close these decisions before implementation

| Decision | Responsible participants | Required recorded result |
|---|---|---|
| 5.1 final scenario and doorway inclusion | Cody, Brendan, team | Chosen activity input, output behavior, final demo boundary |
| 5.2 inventory and per-board fixture | Cody, Sitt, Brendan | Exact board/module, voltages, unique node, pin/power diagram; no conflicts |
| Rule/output behavior | Cody, Sitt | Condition, thresholds/units, debounce or hysteresis, output polarity and fault behavior |
| Display and remote validity | Cody, Brendan, Hasif | Fields, source names, decoding, age limit, reconnect/reset behavior, refresh budget |
| 5.4 SPI device substitution | Brendan | Two real devices, device-specific operations, supported settings, reusable script; approved writable byte/location if storage is used |
| 5.6 ML feature/model contract | Jun Yong, Hasif, Sitt, Cody | Model ID/hash, input order and units, preprocessing, freshness, output meaning and chosen local/remote source |
| ML transport and activation | Hasif, Sitt, Brendan | Agreed bounded payload, topic, status/error response, generation handling and display adapter |

Avoid choosing numeric freshness, timing or persistent-identity policies by
accident. Record the team decision and matching implementation before testing.

## 5. Existing interfaces and implementation gaps

| Path | Existing upstream behavior | D6 work and acceptance |
|---|---|---|
| `pico/<node>/config` | Sensor/rule configuration, same text lines as USB; per-board acknowledgements on `status/config` or `status/rule` | Save complete per-board presets and expected acknowledgements; do not broadcast fixture-specific pins |
| `pico/<node>/cmd` | Commands including RUN/STOP/SHOW and existing ML commands | Verify applied behavior; SHOW's printed output remains USB-console evidence |
| `pico/<node>/sensor/<name>/line` | Protocol/source/length/timestamp plus hex bytes | C must validate topic/source, framing, length and device-specific decoding; physical-unit console JSON is not an MQTT payload contract |
| Presence and heartbeat | Connection presence and periodic uptime | Record connectivity separately from valid, fresh sensor data |
| ML result | Current configurable ML demo prints local results | D4 3.4 must add publishing/local routing; C needs an agreed decoder. Do not claim a current ML MQTT result topic exists |
| LCD output | `lcd-output` has visible test-pattern/text evidence; normal updates are not integrated | Add remote-state adapter and bounded update handoff; test actual displayed values, not only `LCDTEST result=0` |
| Live ML changes | Existing presets require STOP before a model change | Continuous-sampling switch is D4 3.2; show paused switching honestly until the live path passes |
| Remote ML input | Sensor bridging exists; configurable ML bindings currently use local sensors | D4 3.3 must implement typed remote inputs/freshness before demonstrating local ↔ remote switching |
| Configured SPI write/read | Diagnostic/device-layer work exists; task 5.4 is pending | Define/implement supported config-driven transaction semantics with Brendan; a manually invoked diagnostic alone does not close 5.4 |

Reuse existing sensor decoders and publication queues. MQTT callbacks should
validate and enqueue bounded data; they must not draw the LCD, invoke the model
or mutate task-owned state directly. Choose one task owner for LCD drawing;
the existing synchronous driver is single-caller. Do not assume its temporary
BenchmarkTask test owner is the final display architecture.

Keep portable decoding/state in EdgeCore and platform task glue in the MTK
layer. Preserve bounded nonblocking producer sends, error/drop counters,
InferenceTask interpreter ownership and existing ApplicationTask placement.
Budget new typed messages, queues, display state and RAM explicitly.

On C, an expired or failed source must show a clear unavailable/stale state
according to the agreed policy. Never substitute zero for missing data.
Use local receive-age checks unless a different time contract is agreed; do
not subtract uptime timestamps from different boards as synchronized clocks.

## 6. ML interpretation for the demo

Start with B's local AHT fields and an explicitly validated sine-model binding;
do not load the existing all-three-sensor preset on a board missing LIGHT or
DISTANCE. That preset needs all configured inputs fresh. Have the D4 owners
provide the exact smaller preset and expected scalar transformation.

Sine-float/int8 demonstrate configurable execution, not anomaly detection.
The current autoencoder preset repeats features and is also an execution demo.
Display model ID, result and status with that meaning; do not label a raw output
as an anomaly probability, temperature prediction or occupancy count.

If the team selects anomaly detection as the application, add a separate gate:
establish the inherited model's training provenance, full feature construction,
normalization and matching threshold, then validate held-out normal/abnormal
trials. A reconstruction output or arbitrary threshold is insufficient.

## 7. Implementation sequence and task ownership

End-to-end stages remain **NOT RUN**. G0 has a prepared main merge, with LCD
integration and the remaining readiness gates still open. A completed earlier
upstream component does not mark the combined D6 stage passed.

| Stage | Work | Lead/support | Exit gate |
|---|---|---|---|
| G0: scope and baseline | Close section 4 decisions; integrate main and reviewed LCD component | Cody + Brendan; owners review shared changes | Combined clean build/image, regressions, agreed fixtures and interfaces |
| G1: isolated boards | Configure A/B physical sensors and A output; confirm C panel geometry/colors/text | Cody + Sitt; Brendan on LCD | Ground-truth stimulus matches each reading/output; panel visibly correct |
| G2: sensor network | Start broker, unique nodes, A/B publications, independent laptop subscriber | Hasif + Cody | Each source/topic identified; input changes visible over MQTT |
| G3: remote LCD | Implement typed raw-frame decoding, display handoff, validity and failure handling | Brendan + Cody | C shows the same units/value as decoded subscriber evidence; stale sources marked |
| G4: multi-bus/config | Prepare full and piecewise configurations; GPIO/I2C/SPI coexist; byte round trip then device swap | Cody script; Brendan SPI; Sitt fixture | Same image, both configuration styles work; SPI read equals intended byte; replacement works with new config |
| G5: ML result end to end | Local ML first; add agreed result publication and C rendering | Hasif/Sitt + Jun Yong; Brendan display | Real input → successful inference → subscriber → correctly labelled LCD value |
| G6: live changes | Add network model switch and local/remote source switch once D4 provides them | Hasif/Sitt; Cody + Brendan validate | Sampling continues, generations do not mix, bad setup preserves prior working state |
| G7: full rehearsal | Combined workloads and fault trials, capture all evidence and limitations | Cody + Brendan; team | Every required trial has evidence and all unresolved gates remain explicit |

For G4, get actual device protocols from Brendan before writing transaction
payloads. A flash program/read is not interchangeable with an LCD write. Do
not reuse occupied LCD CS pins for flash, erase storage, or repeat the historical
one-time scratch write. Storage writes require a separately agreed disposable
location and device owner approval. Power off before physical substitution.

## 8. End-to-end rehearsal order

1. Record branch/dirty state, actual image hashes, board identities, SDK and
   full fixture/configuration revisions. Start raw serial capture per board.
2. Put laptop and all boards on the same reachable 2.4 GHz network; set local
   ignored credentials, broker address and unique node names, then flash the
   reviewed images. Do not put credentials in plans or evidence archives.
3. Start a broker log and an independent subscriber, for example:

   ```sh
   mosquitto_sub -h <broker-ip> -t 'pico/#' -v
   ```

   Replace the placeholder with the real address. Verify all three nodes are
   connected; heartbeat alone does not prove sensor or LCD operation.
4. Apply approved complete presets to A/B and C's implemented display setup.
   Record ACKs, RUN state and a first valid sample from every displayed source.
   Configuration is volatile; record and reload presets after reset.
5. Change light/activity physically and demonstrate A's agreed output and C's
   matching displayed state. Change B's measured environment using a documented
   safe stimulus and show independent temperature/humidity updates on C.
6. Show G4's multi-bus path on the approved fixture. Compare complete versus
   piecewise configuration, acknowledgements and behavior on the same image.
   Repeated configs can add duplicates: deliberately STOP/CLEAR and reload the
   intended preset when demonstrating full replacement, and label that pause.
7. Show B's numeric ML result locally, at the subscriber and on C. Record model,
   raw features, transformed inputs, outputs, generation and validity.
8. If G6 is implemented, switch models and sources over the network while
   measuring continued sensor output. Otherwise record this portion BLOCKED;
   a STOP-based swap is only the earlier checkpoint, not live acceptance.
9. Run fault trials below separately, restore the known preset after each and
   capture recovery. Save TASKS/resources and independent display evidence.

Runnable per-board `.cfg` files and exact SPI/display/ML commands are next-stage
implementation artifacts after fixture and parser agreement. This plan does
not present proposed interfaces as executable commands.

## 9. Trials and acceptance

Suggested pilot counts/durations are recorded in the worksheet before running;
the team must agree numerical timing, freshness and resource limits. Do not
claim general accuracy or hard real-time bounds from a pilot.

| Trial | Required outcome/evidence |
|---|---|
| Light/activity stimulus | Correct source reading, agreed local output and corresponding LCD update; repeat both transitions |
| Temperature/humidity | C's decoded values/units match source and independently captured MQTT bytes within decoder/display precision |
| Complete then piecewise config | Same intended final state, explicit ACKs; no unintended duplicate sensors or stale bindings |
| SPI byte round trip/device swap | Device, location/operation, write value, read value and new configuration recorded; no firmware rebuild between supported substitutions |
| ML result | Successful actual invocation, correct model meaning and matching subscriber/display result; no console-only result claimed as network delivery |
| Live model/source changes | Continuous acquisition measured; old cached inputs/results never labelled as the new setup |
| Invalid configuration/model | Explicit rejection; previously valid setup continues according to the agreed control contract |
| Malformed/truncated/wrong-source frame | No fabricated reading, no buffer overrun/reset; error/drop diagnostic captured |
| Sensor stops updating | After agreed age limit, C marks that source stale; other sources continue |
| Broker outage/reconnect | Local acquisition/rules/ML continue; C indicates unavailable/stale data; subscriptions and fresh display recover without stale replay |
| Source reset | Volatile configuration loss is visible; old reading is not treated as fresh; reload restores the path |
| C reset | Correct startup and re-subscription; no old values presented as current before fresh data |
| Missing ML feature | Explicit missing/stale result rather than a fabricated successful output |
| Combined load | No unexplained resets, queue losses, invalid display or acquisition stalls; measure stack, heap and drops against agreed limits |

Workloads: W1 = A/B physical sensors and A output; W2 = W1 + MQTT + C LCD;
W3 = W2 + verified ML publication/display; W4 = W3 + live reconfiguration and
approved SPI activity. Use unchanged fixtures/rates or assign a new revision.
Do not silently add D2 flash logging or old D3 `WINDOW` workloads.

## 10. Verification and evidence handoff

For implementation changes run appropriate focused decoder/state/queue tests
and the existing regressions: `sh tools/cody_test.sh`,
`sh tools/d2_host_test.sh`, `sh tools/check_hal_boundary.sh`,
`sh tools/mtk.sh purity`, plus the imported LCD component's tests when relevant.
Build with the pinned SDK, run `sh tools/mtk.sh image`, then D1 `4a`, `5-neg`
and MQTT tests on the correctly isolated fixture before the end-to-end run.
Check TASKS for task placement, stack/heap and error/drop counts. Native tests
and image audits do not replace board evidence.

Store each new session under `parity/logs/mtk/d6/<session-id>/`: completed
worksheet, redacted setup/configs, source/firmware provenance, broker/subscriber
logs, raw per-board serial, display photos/video, stimulus timeline and
resource/timing results. Publication enqueue is not subscriber delivery; only
subscriber capture plus actual screen evidence proves the final path.

Completion requires team review of tasks 5.1–5.7 with evidence, plus each
member's 5.8 explanation of which functions/interfaces change under a new
requirement. Keep D3 board acceptance, D4 remaining work and D2 logging/T-23
statuses independent. Update the shared README and feedback tracker when
implementation/evidence changes, preserving older records.
