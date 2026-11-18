# D6 firmware test cases

Prepared 18 November 2026 on `d6` at `9f3b93c`. **All cases NOT RUN.**
These cases turn the [validation plan](../D6_USECASE_VALIDATION_PLAN.md),
[worksheet](../D6_TRIAL_WORKSHEET.md), [board configurations](BOARD_CONFIGURATIONS.md),
[demo script](DEMO_SCRIPT.md) and [integration status](INTEGRATION_STATUS.md)
into ordered, runnable steps for the current three-board bench. They do not
add new acceptance limits. Where the plan leaves a number or interface
PENDING, the case says so and stays BLOCKED until the decision is recorded.

Roles: **A input only; B ML, storage, data handling and communication; C output.**

## How to use this file

- Run the phases in order. A later phase assumes the earlier phases passed on
  the **same firmware image** and fixture revision. If you change either one,
  start again from the first case it affects.
- Record every attempt in a copy of the worksheet's *per-attempt evidence*
  table, using the case ID (for example `TC-2.3`) and the trial ID it maps to.
  Keep retries as separate attempts.
- Status vocabulary (same as the worksheet): **PASS, FAIL, INCONCLUSIVE,
  BLOCKED, NOT RUN**. A blank field means unknown, not zero and not pass.
- Store evidence under `parity/logs/mtk/d6/<session-id>/`: one raw serial log
  per board, the `mosquitto_sub` capture, photos/video of A's stimulus and C's
  screen, and the completed worksheet. No Wi-Fi passwords or broker
  credentials in any file.
- **Ready now** cases need only the current firmware and bench. **Blocked**
  cases name what must exist before they can run.
- Native tests, a clean build, a heartbeat, an enqueue or an LCD test pattern
  do not prove the end-to-end path on their own (worksheet, review section).

## Bench observed on 18 November 2026

The team's bench photo shows three ESP32-S3 boards, each on its own breadboard
and connected to the laptop by USB:

| Board | What the photo shows | Matches the selected fixture? |
|---|---|---|
| A | Several breakout modules on one breadboard, including an HC-SR04 ultrasonic module and multiple small I2C-style modules | **Check.** The selected D6 fixture is AHT + TSL2561 on SDA1/SCL2. The extra modules (for example the HC-SR04, which the [breadboard guide](../../SENSOR_BREADBOARD_README.md) puts on Trig GPIO4 / Echo GPIO5) must be recorded, and not be on GPIO1/2 |
| B | One small breakout wired to the board | **Check.** The [B raw-transport config](BOARD_CONFIGURATIONS.md#b--raw-transport-configuration) claims no physical bus pins. Record what this module is (for example a planned storage device) and keep it unpowered or unconfigured until its contract is agreed |
| C | ESP32-S3 with a long, narrow SPI LCD | Expected: the reviewed LCD-only fixture (SCK 12, MOSI 11, CS 10, D/C 9, RESET 8, MISO unconnected) and 76×284 panel. Confirm the wiring against [WIRING.md](../../esp32s3_mtk/WIRING.md#separate-lcd-only-breadboard-test-current-lcd_output-firmware) |

A photo is not a wiring check. TC-0.2 records the actual inventory.

## Traceability

| Case | Plan stage | Worksheet trial | Task | Ready now? |
|---|---|---|---|---|
| TC-0.1 – TC-0.3 | G0 | Session/readiness, decision register | 5.2 | Yes |
| TC-1.1 – TC-1.5 | G0/G1 | Readiness (image, boot, D1 regressions) | — | Yes |
| TC-2.1 – TC-2.3 | G1 | Input ground truth for T01/T02 | 5.2 | Yes |
| TC-3.1 – TC-3.2 | G1 | Display bring-up for T02/T06 | 5.5 (partial) | Yes |
| TC-4.1 – TC-4.5 | G2/G4 | T03, transport part of T02 | 5.3 (partial) | Yes |
| TC-5.1 – TC-5.4 | G2/G7 | Transport part of T10, T12, T13 | — | Yes (transport level only) |
| TC-6.x | G3–G7 | T01, T02, T04–T09, T11, T14–T16, storage | 5.1, 5.4–5.7 | Blocked |

---

## Phase 0 — session setup and decisions

### TC-0.1 Record source and firmware identity

- **Preconditions:** `d6` checked out on the laptop that builds and flashes.
- **Steps:**
  1. Record `git rev-parse HEAD` and `git status --short`. Note any local change.
  2. Run `sh tools/mtk.sh check`. Record the reported IDF, compiler and port commits.
  3. Build each node: `NODE=<actual-id> sh tools/mtk.sh build`. Record the
     `.bin`/`.elf` SHA-256 for each node.
- **Pass:** pinned versions match; each board has a recorded image hash.
- **Evidence:** worksheet *Source commit* and *per-board image hash* rows.
  Host metadata is not proof of what is flashed; TC-1.1 captures the boot.

### TC-0.2 Record the actual inventory and pin map

- **Steps:** for each board, list every module, its supply voltage and every
  GPIO it uses. Draw or photograph one labelled pin/power diagram per board.
  Record node ID, board type and serial port.
- **Pass:** no two devices share a GPIO unless they share a bus by design
  (AHT + TSL2561 on I2C GPIO1/2); A uses no LCD pins; C has no input sensor
  wired into the LCD pins; every module's voltage is 3.3 V compatible.
- **Fail:** any pin conflict, unknown module or 5 V signal onto a GPIO.
  Fix before Phase 1.
- **Evidence:** worksheet decision row *A/B/C inventory, unique node/source names, pin/power diagrams*.

### TC-0.3 Record the decision register

- **Steps:** fill every row of the worksheet's decision register with the
  agreed value, owner and date, or leave it PENDING.
- **Pass:** this case passes when the register is filled in honestly. PENDING
  rows are allowed; they keep their dependent Phase 6 cases BLOCKED.

---

## Phase 1 — firmware bring-up on each board (ready now)

Run TC-1.1 to TC-1.4 on **each** board separately, with only that board's
own fixture attached.

### TC-1.1 Flash and boot

- **Steps:**
  1. `NODE=<actual-id> PORT=<port> sh tools/mtk.sh flash`.
  2. `sh tools/mtk.sh monitor`; reset the board and capture the full boot log.
- **Pass:** boot completes with no panic, watchdog or reset loop; the node ID
  printed matches the intended board; Wi-Fi gets an IP address.
- **Fail:** panic/abort, repeated resets, wrong node ID.
- **Evidence:** raw boot log per board.

### TC-1.2 Task placement and resources (`TASKS`)

- **Steps:** at the console, send `TASKS` with nothing configured. Save it.
- **Pass:** ApplicationTask runs on core 1 at priority 23; every task shows
  positive stack headroom; heap is reported; drop/error counters are recorded.
  There is no agreed numeric limit yet, so record the values as the baseline
  for later comparison (TC-4.4, TC-6.10).
- **Evidence:** worksheet *TASKS placement, per-task stack headroom, heap* row.

### TC-1.3 D1 console regressions

- **Preconditions:** confirm the GPIOs these stages touch are not wired to
  anything on this board: `4a` uses GPIO22 as an input; `5-neg` uses I2C
  GPIO16/17 (it expects no device there). Do not run them on a board where
  those pins are in use.
- **Steps:** `sh tools/mtk.sh test 4a`, then `sh tools/mtk.sh test 5-neg`.
- **Pass:** both stages report all steps passed.
- **Evidence:** `parity/logs/mtk/4a.log` and `5-neg.log`; copy them into the session directory.

### TC-1.4 MQTT connectivity

- **Preconditions:** broker running on the laptop; independent subscriber
  started first: `mosquitto_sub -h "$BROKER" -t 'pico/#' -v`.
- **Steps:**
  1. Confirm presence and heartbeat for this node appear at the subscriber.
  2. Publish `SHOW` to `pico/<node>/cmd`; confirm the serial console responds.
  3. On one board only, run `sh tools/mtk.sh mqtt "$BROKER"` (27 steps; uses the node in `wifi_credentials.h`).
- **Pass:** presence/heartbeat seen for all three nodes; commands reach each
  board; the 27-step test passes on the board it was run on.
- **Note:** this proves connectivity only, not sensor data or display output.

### TC-1.5 All three nodes together

- **Steps:** power all three boards; keep the subscriber running for 5 minutes.
- **Pass:** three distinct node IDs, each with presence and regular
  heartbeats; no resets on any serial log.
- **Fail:** duplicate node IDs, one node missing or resetting.

---

## Phase 2 — Board A input ground truth (ready now)

These cases check A's sensors on their own, before any network path.
Use the reference preset from [board_a.cfg.in](../../config/d6/board_a.cfg.in)
via the generator (TC-4.1), or the same lines over the console.

### TC-2.1 I2C devices answer

- **Steps:** load AHT and LIGHT configs on A; check each config ACK; `SHOW`.
- **Pass:** `OK:AHT` and `OK:LIGHT` on `pico/<A>/status/config`; `SHOW`
  lists both with SDA1/SCL2, 100 kHz, AHT at `0x38`, TSL2561 at the address
  of the actual module (`0x29`, `0x39` or `0x49`).
- **Fail:** `init_cfg FAILED` or `FAIL:<name>`. Check wiring and the light
  sensor's ADDR pin before retrying with a different `--light-address`.

### TC-2.2 Temperature/humidity matches a reference

- **Steps:** `RUN` on A. Note the decoded AHT temperature and humidity from
  the console. Read a separate thermometer/hygrometer placed next to the sensor.
  Then apply a safe, documented stimulus (for example breathing near it or
  covering it with a hand for 30 s) and observe the change.
- **Pass:** readings are plausible for the room and move in the right
  direction with the stimulus. Record both readings and the difference; no
  accuracy limit is agreed, so do not claim one.
- **Evidence:** console capture, reference reading, stimulus time on the host/video timeline.

### TC-2.3 Light responds both ways

- **Steps:** with A running, cover the TSL2561, then uncover it; then shine a
  phone torch on it. Repeat each transition twice.
- **Pass:** the raw channels (and decoded value if shown) go down when
  covered and up when lit, every time.
- **Note:** no motion source is selected. Do not use the HC-SR04 as "motion"
  in these cases (plan section 3).

---

## Phase 3 — Board C display bring-up (ready now)

### TC-3.1 LCD test pattern

- **Preconditions:** C on the reviewed LCD-only wiring with the matching
  76×284 panel (TC-0.2). C has no live display path yet.
- **Steps:** generate and run the pattern stage:

  ```sh
  python3 tools/d6_demo.py --node-a "$NODE_A" --node-b "$NODE_B" --node-c "$NODE_C" \
    --broker "$BROKER" --stage lcd-pattern --fixture lcd-only --format shell > /tmp/d6-lcd.sh
  sh /tmp/d6-lcd.sh
  ```

  Or at C's console: `LCDTEST 76 284 82 18 00 0`, then `TASKS`.
- **Pass:** `LCDTEST` returns 0 **and** the photo shows the expected colour
  bands and text, with all four edges visible and nothing cut off or offset.
- **Fail:** nonzero return, wrong colours, shifted/cropped image, blank or white screen.
- **INCONCLUSIVE:** return 0 with no photo.
- **Note:** this is panel bring-up only. It does not show A's readings and
  does not pass T02, T06 or task 5.5.

### TC-3.2 LCD survives a reset

- **Steps:** reset C; repeat TC-3.1.
- **Pass:** same visible result after reset; no change to `TASKS` baseline beyond normal variation.

---

## Phase 4 — raw A → B → C transport (ready now)

Follow [DEMO_SCRIPT.md](DEMO_SCRIPT.md) sections 1–4. This is a transport
rehearsal: it supports G2 and prepares T02, but C does not draw the values.

### TC-4.1 Complete configuration (T03 part 1)

- **Steps:**

  ```sh
  python3 tools/d6_demo.py --node-a "$NODE_A" --node-b "$NODE_B" --node-c "$NODE_C" \
    --broker "$BROKER" --fixture v2-i2c --inputs aht-light --style complete \
    --format shell > /tmp/d6-complete.sh
  ```

  Read the script, then run `sh /tmp/d6-complete.sh`. Type `ok` at each prompt
  only after you see the stated evidence.
- **Pass:** each ACK appears **exactly once**: `OK:VIEW_AHT`, `OK:VIEW_LIGHT`
  on C; `OK:IN_AHT`, `OK:IN_LIGHT` on B; `OK:AHT`, `OK:LIGHT` on A. `SHOW`
  on each board lists only those entries. `TASKS` captured on each board after `RUN`.
- **Fail:** any `FAIL:`, missing ACK, or a duplicate entry in `SHOW`.

### TC-4.2 Data reaches B and C with the same bytes

- **Steps:** with all three running, apply a distinct stimulus on A (TC-2.2
  or TC-2.3) and capture the next frames.
- **Pass:**
  - A: `pico/<A>/sensor/AHT/line` has `proto=i2c src=AHT len=6`.
  - B: `pico/<B>/sensor/IN_AHT/line` has `proto=mqtt src=IN_AHT len=6`
    with the **same hex bytes** as the matching A frame.
  - C: serial shows `proto=mqtt src=VIEW_AHT len=6` with the same bytes.
  - Same for LIGHT / IN_LIGHT / VIEW_LIGHT with `len=8`.
  - The bytes change with the stimulus (not a repeated constant frame).
- **Note:** timestamps differ between boards by design; correlate by bytes
  and host/video time, not by uptime.

### TC-4.3 Piecewise configuration (T03 part 2)

- **Steps:** same as TC-4.1 with `--style piecewise`, same IDs, inputs,
  light address and firmware. Then repeat TC-4.2.
- **Pass:** same ACKs, same `SHOW` result and same A→B→C bytes as the
  complete style. Record the two attempts separately.
- **Note:** both styles start with `STOP`/`CLEAR`; label that pause. The
  GPIO/I2C/SPI part of task 5.3 is TC-6.3, not this case.

### TC-4.4 Short stability run

- **Steps:** leave A→B→C running for 15 minutes. Capture `TASKS` on each
  board at the start and end.
- **Pass:** no reset; frames keep arriving at B and C; drop/error counters
  recorded at both points. Any change in drops must be explained.
- **Note:** this is not T16. The combined workload and its limits are still PENDING.

### TC-4.5 Follow-list limit and `CLEAR` (17 November fix)

- **Preconditions:** B in RUN with the TC-4.1 config.
- **Steps:**
  1. Publish to `pico/<B>/config` further `proto=mqtt` virtual-sensor blocks,
     each following a different `remote_source` on A (for example `X3` … `X9`),
     until B has 9 followed sources.
  2. Record the ACK for each.
  3. Publish MQTT `CLEAR` to `pico/<B>/cmd`, then reload the TC-4.1 config for B only.
- **Pass:** the first 8 follows ACK `OK:<name>`; the 9th ACKs `FAIL:<name>`
  and B's serial says the follow list is full. After `CLEAR`, B prints
  `unfollow: N remote topic(s)` and the reload ACKs `OK` again.
- **Note:** sources that A does not publish are fine for this case; it tests
  configuration, not data.

---

## Phase 5 — transport-level fault cases (ready now)

These test B's input handling and the network path. The C-display half of
each trial needs Phase 6 work, so record these as partial evidence only.

### TC-5.1 Malformed frames are rejected by B (T10, transport part)

- **Preconditions:** TC-4.1 config loaded; B and C running; note B's last
  valid IN_AHT frame. Stop A (`STOP` to `pico/<A>/cmd`) so it does not overwrite the injected frames.
- **Steps:** publish each of these to `pico/<A>/sensor/AHT/line`:

  | Injected payload | What it tests |
  |---|---|
  | `proto=i2c src=AHT len=6 ts=1 : 1c 80 00 00 00` | Too few bytes for `len` |
  | `proto=i2c src=AHT len=6 ts=1 : 1c 80 00 00 00 00 00` | Extra bytes |
  | `proto=i2c src=AHT len=6 ts=1 : 1c 80 zz 00 00 00` | Invalid hex |
  | `proto=i2c src=LIGHT len=6 ts=1 : 1c 80 00 00 00 00` | Wrong source |
  | `1c 80 00 00 00 00` | Bare hex, no envelope |

- **Pass:** for each, B publishes **nothing** on `IN_AHT`, C prints nothing
  new for `VIEW_AHT`, no board resets, and B's rejection/drop diagnostic is
  captured. Finally publish one well-formed frame and confirm it does pass.
- **Remaining for T10:** C must show that a rejected frame does not replace
  or refresh the displayed value (TC-6.4).

### TC-5.2 Broker outage and reconnect (T12, transport part)

- **Preconditions:** TC-4.1 running; note A's config and B/C follows.
- **Steps:** stop Mosquitto for 60 s, then restart it. Keep serial capture
  on all three boards; restart the subscriber after the broker returns.
- **Pass:** A keeps sampling locally during the outage (console); each board
  reconnects; B and C re-subscribe without a new config (followed topics are
  resubscribed on connect) and fresh A→B→C frames resume; no reset.
- **Record:** time of outage, reconnect per board, first fresh frame at B and C.
- **Remaining for T12:** C must show unavailable/stale during the outage and
  fresh values after (TC-6.6).

### TC-5.3 Source board reset (T13, transport part)

- **Steps:** reset A. Watch B and C.
- **Pass:** A comes back with **no** sensors configured (config is volatile)
  and this is visible in its boot log and `SHOW`; B/C receive nothing new;
  after reloading A's config (TC-4.1 A step) frames resume to B and C.
- **Remaining for T13:** C must not present the old reading as fresh (TC-6.6).

### TC-5.4 Receiver board reset

- **Steps:** reset B; reload its config; then reset C; reload its config.
- **Pass:** each comes back unconfigured, ACKs the reload once, and the
  A→B→C path resumes with new bytes.

---

## Phase 6 — blocked cases (prepare, do not run yet)

Each case lists what must exist first. Keep it BLOCKED until then; do not
substitute console output, a synthetic event or an LCD test pattern.

| Case | Trial | What it will show | Blocked on (owner) |
|---|---|---|---|
| TC-6.1 | T02 | A temperature/humidity → B → C LCD; C's value and unit match A's bytes within display precision | B→C typed payload/topic and C display adapter (Brendan; Codi expected values) |
| TC-6.2 | T01 | A light/activity → B routing → C output, both transitions | Motion source, rule/output behaviour and thresholds (5.1; Cody, Sitt, Brendan) |
| TC-6.3 | T04 | GPIO, I2C and SPI working together on one board; SPI byte write equals read | SPI devices, operations and approved write location (5.4; Brendan). The `mtk.sh spi` loopback needs GPIO12↔13 and **cannot run on C with the LCD wired** |
| TC-6.4 | T10 (display) | Rejected frame does not change or refresh C's value | TC-6.1 plus TC-5.1 |
| TC-6.5 | T11 | Stop one sensor; C marks it stale after the agreed age; the other keeps updating | Freshness/age limit decision and TC-6.1 |
| TC-6.6 | T12–T14 (display) | C shows unavailable during outage/reset, then fresh data; no stale replay | TC-6.1 and freshness decision |
| TC-6.7 | T05 | Swap SPI device after power-off, reconfigure, same image | TC-6.3 and second device (Brendan) |
| TC-6.8 | T06 | A input → B ML → subscriber → C labelled result | D4 remote bindings and result topic (Hasif/Sitt, Jun Yong); C rendering (Brendan) |
| TC-6.9 | T07–T09, T15 | Live model change; source switch; bad model keeps old state; missing input gives explicit invalid result | D4 3.2/3.3 live switching and activation semantics |
| TC-6.10 | T16 | Combined load W1–W4 with resources against agreed limits | Workload, duration and limits decision; all above |
| TC-6.11 | Storage | B stores records, reads back, handles errors | Storage device, record format, writer and failure contract (Brendan, Jun Yong). No flash write or erase until agreed |

When a blocker is cleared, write the case out in full here (preconditions,
steps, pass/fail, evidence) before running it, and add the decision to the
worksheet's register.

---

## Session summary checklist

At the end of each session, fill the worksheet's *Review and remaining gates*:

- [ ] Cases attempted, with status, attempt IDs and evidence file names
- [ ] FAIL/INCONCLUSIVE cases with owner and next action
- [ ] `TASKS` before/after and any resets explained
- [ ] Which worksheet trials now have partial evidence (Phase 4–5) and which remain NOT RUN
- [ ] Evidence directory and reviewer/date
- [ ] [Integration status](INTEGRATION_STATUS.md) and README updated with a dated entry; no trial marked PASS without board, subscriber and (where required) screen evidence
