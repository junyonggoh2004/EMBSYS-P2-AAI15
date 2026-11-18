# D6 configuration and demo script

Prepared 11 November 2026. **Physical stages NOT RUN.** This is Codi's
integration script with Brendan's LCD/output and SPI/storage handoffs.
Read the [board configurations](BOARD_CONFIGURATIONS.md) first.
The user selected the existing AHT + TSL2561 fixture on A and reviewed
LCD-only wiring on C on 11 November. The commands below use that selection;
physical verification remains NOT RUN. Motion and B storage remain separate.

## 1. Record the setup and start evidence capture

Record actual A/B/C hardware, node IDs, serial ports, image hashes, fixture
diagrams, broker/network, expected sensor addresses and operator in a copy of
the [worksheet](../D6_TRIAL_WORKSHEET.md). A is input only; B is processing,
storage and communication; C is output. Do not reuse a fixture with conflicting
pins. Keep the firmware image unchanged between the two configuration styles.

Open one serial monitor per board and a separate broker subscriber. Save all
four streams under `parity/logs/mtk/d6/<session-id>/` and use one host/video
timeline for physical stimulus correlation. In a terminal, set the real values:

```sh
export BROKER='your-broker-host'
export NODE_A='your-input-node'
export NODE_B='your-processing-node'
export NODE_C='your-output-node'
mosquitto_sub -h "$BROKER" -t 'pico/#' -v
```

Capture subscriber output with your normal logging method. Start it before
publishing commands so non-retained ACKs are not missed. Presence/heartbeat
shows connectivity only. Never save Wi-Fi passwords or broker credentials in
the worksheet, scripts or committed captures.

## 2. Generate and review the complete-configuration script

The generator has no serial/network imports and performs no hardware actions.
Its default JSON output includes every topic/payload and expected observation:

```sh
python3 tools/d6_demo.py --node-a "$NODE_A" --node-b "$NODE_B" --node-c "$NODE_C" \
  --broker "$BROKER" --fixture v2-i2c --inputs aht-light --style complete \
  > /tmp/d6-complete.json

python3 tools/d6_demo.py --node-a "$NODE_A" --node-b "$NODE_B" --node-c "$NODE_C" \
  --broker "$BROKER" --fixture v2-i2c --inputs aht-light --style complete \
  --format shell > /tmp/d6-complete.sh
```

Select `--inputs aht` for the smaller temperature/humidity-only trial. Use
`--light-address 0x39` or `0x49` only for a matching actual light module.
Read the generated script before running it. It addresses only the named
nodes; nothing is broadcast or retained. Applying it replaces volatile sensor,
rule and bridge configuration on all three boards with `STOP`/`CLEAR` and
stops inference with `ML STOP`.

After review, run interactively:

```sh
sh /tmp/d6-complete.sh
```

The script checks CLI command exit codes and waits for the operator to verify
each stated serial/subscriber condition. Type `ok` only when it is observed;
any other response or EOF stops the script. Missing/FAIL ACKs are not success.
Grouped configuration is not atomic: earlier blocks may already have applied
if a later block fails. No automatic rollback is claimed.
These are manual evidence checks, not automated board verdicts. No retries
are hidden; preserve a failed attempt and restore/re-run explicitly.

The sequence is:

1. STOP physical input A first, then B/C; `ML STOP` on each.
2. MQTT CLEAR C, load its complete config as one payload, inspect SHOW.
3. MQTT CLEAR B, load its complete config as one payload, inspect SHOW.
4. MQTT CLEAR A, load its complete config as one payload, inspect SHOW.
5. RUN C, B, A; capture TASKS on each.

Expected config ACKs are `OK:VIEW_AHT`/`OK:VIEW_LIGHT` on C,
`OK:IN_AHT`/`OK:IN_LIGHT` on B and `OK:AHT`/`OK:LIGHT` on A, on each
node's `pico/<node>/status/config`. AHT-only omits all LIGHT entries. Each
configuration should appear exactly once. A subscription enqueue/config ACK
does not establish that data reached another board.

## 3. Demonstrate the raw A→B→C path

This is a transport rehearsal supporting G2 and preparing T02; it does not
pass T02's live LCD requirement.

| Check | Required observation |
|---|---|
| A source | `pico/<A>/sensor/AHT/line`: `proto=i2c src=AHT len=6 ...` with valid bytes |
| B handling | `pico/<B>/sensor/IN_AHT/line`: `proto=mqtt src=IN_AHT len=6 ...`; same bytes as the correlated A frame |
| C receipt | C serial shows `proto=mqtt src=VIEW_AHT len=6 ...`; optional C diagnostic publication confirms its bridge ran |
| Optional light | A `LIGHT`, B `IN_LIGHT`, C `VIEW_LIGHT`, length 8 and matching bytes |
| Physical change | Safe change to A's environment/light produces a corresponding new A→B→C byte sequence; record stimulus time and each capture position |
| Resources | No unexplained reset; record TASKS stack/heap and error/drop counters before/after |

Select a distinct observed change, not a repeated constant frame, for
correlation. Record uncertainties and dropped samples. Different uptime
timestamps are expected because each bridge re-emits with local time.
Physical units require the configured AHT/TSL decoder; the generic bridge's
hex output is not a typed ML feature or a displayed value.

## 4. Repeat with piecewise configuration

Generate the same plan with `--style piecewise --format shell`, saving to
`/tmp/d6-piecewise.sh`, then inspect and run it interactively. Keep actual
IDs, input selection, sensor address and firmware identical to stage 2.

Here each sensor/virtual-sensor block is a separate config publication.
This is **one complete sensor block at a time**, not incremental edits to an
existing sensor or one field per MQTT message. Both styles explicitly
STOP/CLEAR and re-create the intended state. Compare SHOW, all ACKs, raw path
and resource observations. Label the intentional pause; this is not a
continuous live model switch. Preserve a distinct T03 attempt for each style.
This covers the configuration-loading portion of task 5.3; the required
GPIO/I2C/SPI demonstration on one board remains a separate fixture/stage.
With AHT only there is one block per board, so the two styles have identical
payload grouping. Use the matching AHT+LIGHT fixture to demonstrate grouping
multiple blocks versus separate publications.

## 5. Run C's independent LCD pattern

Only on Brendan's reviewed `lcd-only` wiring and matching 76×284 panel:

```sh
python3 tools/d6_demo.py --node-a "$NODE_A" --node-b "$NODE_B" --node-c "$NODE_C" \
  --broker "$BROKER" --stage lcd-pattern --fixture lcd-only --format shell \
  > /tmp/d6-lcd.sh
sh /tmp/d6-lcd.sh
```

Capture the `LCDTEST` return code, actual bands/text/edges, a photo/video and
TASKS. Nonzero return or incorrect screen is FAIL; missing evidence is
INCONCLUSIVE/NOT RUN. The pattern is a diagnostic and closes after drawing;
it does not show A's received readings. Record it as panel bring-up, not a
completed T02/T06 or task 5.5.

## 6. Handoffs before the full demonstration

| Remaining stage | Owner / required artifact | Trial impact |
|---|---|---|
| Live output | Brendan: agreed B→C typed payload, C state/handoff/rendering and invalid/stale handling; Codi: expected values and correlation script | T01/T02/T10–T14 remain open |
| ML | Hasif/Sitt + Jun Yong: B's typed A-input bindings, model contract and result publication; Brendan renders the labelled result | T06–T09/T15 remain open |
| Storage | Brendan + Jun Yong: approved B device/pins, data/identity mapping, bounded writer, errors/readback and dedicated acceptance trials | No storage PASS or write command supplied here |
| Multi-bus/device swap | Brendan: actual two-device protocols and config operations; Codi: combined script and fixture revision | T04/T05 remain open |
| Full rehearsal | Codi + Brendan, with the owners above: agreed durations/limits, real outputs and failure recovery evidence | T16 and 5.7 remain open |

Do not fill missing stages with speculative ML/LCD/storage commands, a
synthetic `APP EVENT`, historical scratch writes or an automatic erase. Store
partial results honestly and update the worksheet only from actual evidence.

## Native verification of these scripts

`python3 -m unittest discover -s tests -p test_d6_demo.py` compiles the real
portable config/timing parsers and checks complete/piecewise equivalence,
board source direction, AHT-only selection, identity/fixture rejection and
generated shell syntax. This does not prove subscriptions, physical sensors,
LCD output or board timing. Existing firmware integration evidence is in
[integration status](INTEGRATION_STATUS.md).

The D6 native suite and six focused demo checks passed on 11 November 2026.
Retained [native logs and provenance](../../parity/logs/mtk/d6/native-20261111-config-demo/provenance.txt)
record the source base and separate final focused rerun. No physical setup was
selected or exercised by those tests.
