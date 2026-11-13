# D6 integration handoff: doorway inputs, processing, LCD

Recorded 13 November 2026 from the project chat. This is the selected setup
direction and a work plan, **not evidence of a completed three-board run**.
`D6-Integration` starts from `origin/d6` at `9121286`, preserving its code,
tests, documentation and archived doorway plan. The current D6 worksheet still
needs actual fixture decisions and observations before a trial can pass.

## Selected A -> B -> C behaviour

The user selected the doorway use case with light, temperature, humidity,
LD2450 radar and two sonars as inputs. Board roles are:

| Board | Responsibility | Does not own |
|---|---|---|
| A: acquisition | Read AHT and TSL2561; acquire Sonar A/B on its own clock; use radar to open and close the time-sensitive D3 `WINDOW`; assemble one complete crossing event per window and send it to B | Final occupancy count, ML, persistent storage or display |
| B: decision/storage | Validate and deduplicate A events; decide the final entry/exit outcome; be the sole writer of occupancy; later run ML; write agreed records to SPI flash; publish readings, occupancy and status snapshots to C | Physical doorway timing or LCD drawing |
| C: output | Display B's latest valid readings, occupancy and system status; mark values stale when updates stop | Sensor acquisition, occupancy decisions or flash writes |

This refines the 11 November shorthand that A is "input only": A must perform
**local temporal acquisition and window assembly** to preserve sonar timing.
B makes the final application decision and changes occupancy. The previous
generic room-monitor proposal is superseded for this integration plan; the
archived [doorway plan](reference/D6_USECASE_VALIDATION_PLAN.md) remains useful
for geometry, timing, faults and evidence. No current implementation is claimed
to already transport a complete crossing event from A to B.

The current D3 `WINDOW` path also offers its event to a **local** ApplicationTask,
which can change local occupancy. The selected three-board mode must prevent
that A-side count and route the completed window event to B instead. Reusing
the existing local behaviour unchanged would give two occupancy authorities
and would not satisfy this architecture.

### Crossing event contract to agree and implement

One event per completed window should carry at least the A node identity,
boot identity, monotonically increasing event/sequence ID, entry/exit
**candidate**, observed A/B order and validity, quality/fault flags, and the
features later needed by B's ML model. Record the radar/window outcome and
the local acquisition times or durations required for review. A uses its own
clock for sonar pairing and the radar window; B must not use MQTT delay or
subtract separate board uptimes as if they were synchronized. B acknowledges
or logs rejection, deduplicates by identity, and applies at most one final
occupancy change per accepted event. Decide the wire schema, bounds, replay
policy and recovery before a physical counting claim.

Environmental readings may follow the existing identified sensor-frame path
from A to B. B converts them using explicit sensor profiles and forwards
agreed values/status to C. A raw distance is not an occupancy result.

## Materials: total for three boards

Subtract parts already in hand. Exact module revision, power and Echo levels
must be checked before the wiring is approved.

| Item | Quantity | Board/use |
|---|---:|---|
| ESP32-S3 Super Mini | 3 | A, B, C |
| Verified 3.3 V-compatible HC-SR04+ | 2 | A, Sonar A and B |
| HLK-LD2450 | 1 | A, radar |
| TSL2561 light module | 1 | A, I2C |
| AHT temperature/humidity module | 1 | A, I2C |
| W25QXX external SPI flash breakout | 1 | B, storage fixture |
| DIYUSER 76 x 284 ST7789P3 LCD | 1 | C, output |
| Breadboard | 3 | One per board, with known continuous rail sections |
| USB data/power cable | 3 | One per board |
| Assorted compatible jumper leads/adapters | Approximately 50-60 | Includes spares and connections to module headers |
| Suitable four-wire radar connector or soldered leads | 1 set | The bare LD2450 cannot be reliably plugged into a breadboard by loose wire contact |
| Multimeter | 1 | Check rails and actual signal levels |
| Logic analyzer or oscilloscope | Access to 1 | Trigger/Echo guard, order and complete-pair timing evidence |
| Broker laptop and reachable Wi-Fi | 1 setup | MQTT capture and configuration |
| Second readable SPI device | 1, model pending | D6 5.4 swap/reconfiguration; select protocol before purchase |

Board A's USB-powered ESP32 **5V** pin may supply the LD2450's 5V input for
the initial fixture, as the user confirmed from the professor's approach.
Share A's ground, keep 5V away from 3V3 and GPIO, and verify the USB/current
budget under load. Unplug USB while wiring. A standard 5V Echo needs verified
level protection; the listed 3.3V-compatible sonar modules must be identified
and measured rather than assumed from their name.

## Breadboard and pinout images

The images show electrically connected breadboard rows and separate power
rails. They are **schematic proposals**, not photos of the completed three-board
fixture. A-E holes on one numbered terminal row are joined; F-J are a different
group across the center gap. Check any split power rail for continuity.

| Board | Image | Pin status |
|---|---|
| A | [Inputs, radar, two sonars and I2C](images/board-a-inputs.png) | I2C GPIO1/2 and Sonar A GPIO4/5 follow the existing V2 fixture. Sonar B GPIO6/7 and radar UART GPIO17/18 are **proposals requiring the team pin/power review**. GPIO17/18 are underside pads on the pictured HW-747. |
| B | [SPI flash](images/board-b-processing-flash.png) | Previously tested D2 six-wire map: VCC 3V3, GND, CS GPIO10, SCK GPIO11, MOSI/DI GPIO12, MISO/DO GPIO13. The complete D6 board and storage write path are untested. |
| C | [LCD](images/board-c-lcd.png) | Verified isolated fixture: VCC 3V3, GND, SCK GPIO12, MOSI GPIO11, RST GPIO8, DC GPIO9, CS GPIO10, and module-specific active-low BL to GND. The imported D6 diagnostic still needs a visible combined-firmware check. |

On A, the **3V3 rail** supplies the AHT, TSL2561 and verified 3.3V sonars;
the **separate 5V rail** supplies only the radar; all grounds join A's GND.
Radar TX connects to ESP UART RX, and radar RX to ESP UART TX if the final
UART configuration requires both. No signal wire joins A, B and C; the final
application path is A -> broker -> B -> broker -> C.

The older [shared breadboard wiring](../../esp32s3_mtk/WIRING.md) and the
isolated LCD fixture are different layouts. Do not merge their pins by name.
The earlier [LCD-plus-radar prototype](reference/LCD_RADAR_PROTOTYPE_LEGACY.md)
was transferred for history; it puts radar and LCD on one board with a
different power/UART proposal and must not be used as the D6 fixture.

## Implementation order and exit gates

| Gate | Work | Evidence before the next gate |
|---|---|---|
| G0. Baseline and decisions | Record selected scenario, actual parts/revisions, proposed A/B/C diagrams, node names, power, message fields, freshness, storage content and owners. Build/audit this D6 firmware. | Reviewed decisions, conflict-free approved wiring, source/image provenance, native regressions. |
| G1. Isolated components | On A test AHT and TSL; test each sonar separately including missing Echo and cancellation; test radar raw UART separately. On B read flash JEDEC ID only. On C run `LCDTEST 76 284 82 18 00 0`. | Physical readings and fault behaviour; flash ID; visible LCD colors/edges/text; TASKS headroom. Existing isolated LCD diagnostics remain available. |
| G2. Local doorway acquisition on A | Place A/B regions to expose clear -> A -> both -> B -> clear and the reverse. Measure guard, useful range and full pair latency. Add radar `WINDOW` trigger and assemble one crossing candidate/event locally. | Matched ground-truth sequences, <=50 ms complete pair ceiling where required, no unsupported event on ambiguous/fault observations. Keep trials and timing traces. |
| G3. Minimal A -> B transport | Agree bounded crossing-event schema and publish one event per completed window. B validates identity/order/flags, rejects malformed or duplicate events, and becomes the only occupancy writer. | A event, broker subscriber capture, B acceptance/rejection and occupancy delta correlate for entry, exit and negative trials. |
| G4. B storage | Define record format, disposable flash area, writer ownership, failure policy and recovery. Add writes only after those decisions. | Readback/recovery evidence and explicit fault outcome; no blind erase or reuse of prior one-time scratch writes. |
| G5. B -> C live display | Agree output payload/topic and profile decoder. C uses one owner and a bounded handoff to draw latest B snapshot, with validity/age limits. | Subscriber capture agrees with B and visible LCD values; stopped source or outage becomes stale without substituting zero. |
| G6. Configurable multi-bus demo | On an approved single-board fixture demonstrate GPIO, I2C and SPI; complete and piecewise config; one-byte write/read; safe SPI device substitution. | Same firmware image, explicit ACKs, intended byte equals readback, second device reconfigured and working. This is separate from flash-log acceptance. |
| G7. ML and full rehearsal | Add B's typed remote ML input and labelled result transport when D4 is ready; run combined workloads, faults, reset/reconnect and resource checks. | A -> B inference/decision/storage -> C result with worksheet and raw evidence. No model output labelled occupancy probability without its validated meaning. |

The user's staging preference is to bring up **inputs, processing/storage, then
LCD**, and to integrate the advanced MQTT configuration and ML portions later.
Minimal MQTT is nevertheless necessary at G3 and G5 for the actual three-board
path. Component tests can precede it; a live C display or B's real doorway
count cannot be claimed before A -> B -> C transport exists.

## D6 checklist mapping and current status

| Item | Next concrete work | Status at handoff |
|---|---|---|
| 5.1 Use case | Record the user's doorway selection and secure team fixture/interface review | User direction recorded here; team acceptance and physical trial pending |
| 5.2 Board definitions | Finalize A sensor pins/power, B flash/decision contract, C display map and node/topic names | Proposed diagrams only |
| 5.3 Multi-bus demo | Select one-board fixture, write full then piecewise config script and expected observations | Script and physical test pending |
| 5.4 SPI config write/read/swap | Select two real devices and implement device-aware configured transaction/readback | Pending; existing flash diagnostic is insufficient |
| 5.5 Live LCD | Implement B -> C snapshot, decoding, stale state and display owner | LCD diagnostic imported; live path pending |
| 5.6-5.8 | ML and full rehearsal after the above gates | Pending, later scope |

The `d6` base includes an imported `LCDTEST` diagnostic and strict MQTT
sensor-frame parser with native/build evidence in
[integration status](INTEGRATION_STATUS.md). It does not contain a completed
remote WINDOW-event pipeline, B storage writer, or live B -> C screen. The
`lcd-output` at `9b13e36` contains separate `FLASHLCD` and
`LCDMOCK` tests; those were **not merged into this branch** and must be
reviewed and tested independently if selected. Preserve the isolated LCD test
and earlier fixture documentation during future integration.

Use the [D6 worksheet](../D6_TRIAL_WORKSHEET.md) for actual decisions, session
provenance, serial/MQTT logs, physical observations and pass/fail outcomes.
