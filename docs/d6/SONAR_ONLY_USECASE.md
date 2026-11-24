# Current D6 doorway use case: two sonars, no radar

**Decision recorded 21 November 2026.** The team removed the LD2450 radar from
the intended D6 demonstration. This document is the current application
direction for Boards A, B and C. Earlier radar diagrams, dated bring-up
records and D3 `WINDOW` documentation remain historical or component references;
they are not instructions for the current Board A fixture.

## Intended behavior

Board A has one ESP32-S3 N16R8, an AHT temperature/humidity module, a TSL2561
light module and two verified 3.3 V-compatible ultrasonic modules. AHT and
light share I2C GPIO1 SDA/GPIO2 SCL. Sonar A uses GPIO10 TRIG/GPIO11 ECHO and
Sonar B uses GPIO6 TRIG/GPIO7 ECHO. The [current Board A config](../../config/board_a.cfg)
contains exactly these four sensor profiles, with no radar profile. Use the
[current wiring guide](BOARD_A_SONAR_ONLY_WIRING.md); the earlier GPIO4/5 and
breadboard signal-row diagrams describe older fixtures.

Board A can run an opt-in doorway counter with alternating pings. With A on
the outside and B on the inside, a confirmed clear → A → both → B → clear
sequence counts one entry. The reverse sequence counts one exit. Aim the two
sensors across the doorway at a stable wall or flat backboard. Adjust the zones
so a single person can briefly block both while crossing. Standing still,
turning back, ambiguous order, and a crossing that times out leave the totals
unchanged.

### Run the doorway counter on Board A

Build and flash Board A with its [N16R8 board setup](N16R8_BOARD_SETUP.md),
then open **MTK: monitor** in VS Code. Enter
`STOP` and `CLEAR`, paste the four lines from
[`config/board_a.cfg`](../../config/board_a.cfg), and enter these commands:

```text
DOORWAY SONAR_A SONAR_B 70 85 70 85 70 2000
RUN
DOORWAY STATUS
```

The four distance arguments are A near, A clear, B near, and B clear, in
centimetres. The last two arguments are the time between alternating pings
and the maximum time from first confirmed blockage to confirmed clear, in
milliseconds. The example assumes stable empty echoes beyond 85 cm and
occupied echoes of 70 cm or closer on both sensors. Measure both conditions
in your doorway and adjust each threshold separately if needed.

| Reading with the example settings | Counter behavior |
| --- | --- |
| Valid echo from 2 to 70 cm | Two consecutive readings mark that zone occupied. |
| Valid echo of 85 cm or more | Two consecutive readings mark that zone clear. |
| Valid echo between 70 and 85 cm | The previous zone state holds. |
| `distance_cm: null` or invalid echo | The crossing is dropped. Wait for both zones to confirm clear before counting again. |

The 20 ms echo timeout in the board configuration limits the theoretical range
to about 343 cm at room temperature. Set up a reliable empty echo well within
that distance. At the example cadence A pings, B pings at least 70 ms later, and
A pings again at least 70 ms after B. Each sensor is sampled about every
140 ms under normal load. Consecutive confirmations on both channels that
land on adjacent pings are rejected as ambiguous. Walk at different speeds in
both directions to check zone spacing and overlap. Two people in the doorway
at once can be indistinguishable from one crossing or an ambiguous sequence.

Wait for `[DOORWAY] READY`, then check for one `ENTRY` or `EXIT` per full
crossing. Try standing in one zone, turning back after blocking both, and
blocking the background echo. `DOORWAY STATUS` displays Board A's session
totals. `STOP` pauses and `RUN` resumes after both zones clear again.
Use `STOP` then `DOORWAY OFF` to return to ordinary polling or run the
`PAIR` diagnostic. A new doorway setup, `CLEAR`, or reboot starts a new
session.

A publishes identified environmental frames. The cross-board design calls for
one bounded crossing candidate per completed sequence with node/boot identity, sequence
number, direction candidate, local timing and quality/fault flags. B validates
and deduplicates candidates, owns the final occupancy count and agreed SPI
flash record, and publishes readings/count/status to C over MQTT. C displays
the latest valid values, occupancy and stale/fault status on the LCD. A's
local clock establishes sonar order; independent board uptimes are not a
synchronized cross-board clock. B remains the only occupancy writer.

## What the current firmware does and does not do

The current [Board A config](../../config/board_a.cfg) can acquire/publish AHT,
light and two periodic sonar readings. The GPIO pulse driver now makes a
second due sonar wait for the first capture to finish; the change is recorded
in the [19 November Board A bring-up](BOARD_A_BRINGUP_20261119.md). The `PAIR`
command is a **one-shot diagnostic** while stopped: it captures both sonars
in sequence within a 50 ms pair budget. Its guard is the delay between
measurements, not the seconds-long interval for a person to cross a doorway.

The existing D3 `WINDOW` command still requires a radar UART source. For
the sonar-only demo, use `DOORWAY`. The new counter reports local entries and
exits. The separate [Board B
`people-counter` live test](../commands/ML_commands.md#4-people-counter--experimental-live-31-reading-window)
uses dedicated A/B 70 ms profiles for one sonar and a rolling 31-reading
window. Its prediction does not change occupancy. Reload the normal four-sensor
profiles after that test. The live doorway counter uses both sonars.

## Evidence

The [19 November record](BOARD_A_BRINGUP_20261119.md) covers Board A's first sensor checks.
Both sonars were then read in sequence on the final wiring (D3-3), and the full three-board
setup ran at the demo on 24 November; see the [D6 checklist](../../README.md#26-d6-task-checklist).
