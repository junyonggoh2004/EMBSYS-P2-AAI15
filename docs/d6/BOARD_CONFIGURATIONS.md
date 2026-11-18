# D6 board configurations

Prepared 11 November 2026 on `d6` at `9121286`. Roles are confirmed by the
user: **A input only; B ML/storage/data handling/communication; C output**.
The user selected the existing fixtures on 11 November: **AHT + TSL2561 on A
(SDA1/SCL2), and the reviewed LCD-only wiring on C**. The default
`--inputs aht-light` matches this selection; AHT-only remains a reduced
troubleshooting profile. Motion and B's storage fixture remain separate.
Actual board inventory,
wiring and node IDs must be recorded in the [worksheet](../D6_TRIAL_WORKSHEET.md)
before a physical run. All new demo stages are **NOT RUN**.

## Configuration baseline and ownership

Use the active `esp32s3_mtk` firmware on all three boards, with distinct actual
node IDs and a shared reachable broker/network. Node IDs and Wi-Fi/broker
settings come from the existing build setup, not these sensor configurations.
`NODE=<actual-id> sh tools/mtk.sh build` selects a separate build when needed;
flashing is a separate explicit operation. Record each actual image/hash.

| Board | Configuration in this checkpoint | Full D6 responsibility | Lead and handoff |
|---|---|---|---|
| A | AHT + TSL2561 using the selected existing V2 I2C fixture | Acquire all selected physical inputs; publish identified raw frames | Codi defines presets/trials with Sitt; motion source still to select |
| B | Follow A's raw frames using `proto=mqtt`, validate and re-publish under `IN_*` aliases | Typed handling, ML on A inputs, storage and onward communication | Codi coordinates integration; Hasif/Sitt own ML/network changes; Brendan/Jun Yong own storage |
| C | Follow B's `IN_*` streams under `VIEW_*` aliases; separate LCD diagnostic | Render B's agreed values/results/validity and drive selected outputs | Brendan owns display/output adapter; Codi defines expected fields and verifies the full path |

The current relay proves a transport path only when real subscriber/serial
evidence is captured. It does not populate the LCD, supply typed remote ML
features, or store records. `freq_hz=1` in virtual sensors is configuration
metadata: forwarded frames arrive at A's actual rate, not a new 1 Hz sampler.

## A — `v2-i2c` reference fixture

| Device/signal | Setting |
|---|---|
| AHT temperature/humidity | I2C `0x38`, SDA GPIO1, SCL GPIO2, 100 kHz |
| TSL2561 light, optional | Same bus; default `0x29`; select `0x39`/`0x49` only to match the actual module |
| Power/ground | Existing 3.3 V-compatible reference modules and common ground; check the [breadboard guide](../../SENSOR_BREADBOARD_README.md) |
| Acquisition | Existing preset's 0.5 Hz requested rate and 600 ms acquisition deadline; these are fixture settings, not D6 latency/freshness acceptance limits |
| Source names | `AHT`, optionally `LIGHT` |

Template: [board_a.cfg.in](../../config/d6/board_a.cfg.in). There is no output
rule, local inference, motion classification or doorway counting in this
profile. HC-SR04+ distance is not silently relabelled as motion. Add the
selected activity input only after its device/decision rule is specified.

## B — raw transport configuration

Template: [board_b.cfg.in](../../config/d6/board_b.cfg.in). It claims no local
physical bus pins; the MQTT virtual-sensor path does not open a sensor bus.

| Input topic | Local alias / outgoing topic | Bytes |
|---|---|---|
| `pico/<A>/sensor/AHT/line` | `IN_AHT` / `pico/<B>/sensor/IN_AHT/line` | Original six AHT bytes |
| `pico/<A>/sensor/LIGHT/line`, optional | `IN_LIGHT` / `pico/<B>/sensor/IN_LIGHT/line` | Original eight TSL2561 bytes |

The existing bridge validates framing and source identity, then re-publishes
with `proto=mqtt`, the local alias and a new local timestamp. It is not a
provenance-preserving application envelope: correlate bytes and host/video
time, not equal board uptimes. The bridge does not infer the physical decoder
or refresh/stale policy from a name.

ML, storage device/pins, record content/identity, bounded writer ownership,
failure behavior and production B→C payload remain implementation handoffs.
No model setup, flash write, scratch reuse or erase command is generated.
This raw profile does not close D4 remote input or D2 logging/T-23.

## C — receiver and separate `lcd-only` fixture

Template: [board_c.cfg.in](../../config/d6/board_c.cfg.in). C follows **B**,
not A: `IN_AHT → VIEW_AHT`, optionally `IN_LIGHT → VIEW_LIGHT`. Receipt can
be observed in C serial output and its diagnostic re-publication. No physical
input sensor is configured on C.

The separate LCD test uses the reviewed fixture in
[WIRING.md](../../esp32s3_mtk/WIRING.md#separate-lcd-only-breadboard-test-current-lcd_output-firmware):

| ST7789 signal | ESP32-S3 GPIO |
|---|---:|
| SCK | 12 |
| MOSI | 11 |
| CS | 10 |
| D/C | 9 |
| RESET | 8 |
| MISO | Unconnected |

Power and BL follow that identified module's wiring, including its existing
active-low BL arrangement; this is not a general backlight prescription.
The earlier shared-breadboard LCD map is different. For the reference
76×284 panel only, the current diagnostic is:

```text
LCDTEST 76 284 82 18 00 0
TASKS
```

The driver closes after the pattern test. `RUN` and remote sensor frames do
not draw values on the screen. Brendan's next adapter must connect B's agreed
typed output/status to C's single drawing owner, including invalid/stale and
reset handling. Define other output devices before adding their pins/actions.

## Operational constraints

- The generator requires explicit actual A/B/C IDs and broker. `.cfg.in`
  templates contain substitution markers and are not directly loadable.
- Use MQTT `CLEAR` in this checkpoint: it resets scheduler/rules, **bridge
  mappings and followed-topic subscriptions** (at most 8 per board; a ninth
  follow is refused with a config `FAIL`). The current serial REPL's `CLEAR` does not reset bridge mappings.
  Do not use the generic three-sensor serial loader for B/C.
- `STOP` gates local acquisition, not incoming bridge callbacks. The demo
  stops A first and configures receivers before restarting A. Late queued
  frames can still appear; identify fresh physical stimuli in the capture.
- `ML STOP` is sent to all boards for this transport checkpoint, avoiding a
  leftover local model setup. The final B ML demonstration is a separate stage.
- Configuration is volatile. After a reset, stop A and reload the complete
  reviewed setup; capture the reset and recovery as a separate attempt.
- The broker can also be the laptop in this topology. B owns the application's
  handling/communication path; it need not host the broker.

Use the [demo script](DEMO_SCRIPT.md) for complete/piecewise loading and the
per-stage evidence checklist. Production mappings and physical acceptance
remain in the current [validation plan](../D6_USECASE_VALIDATION_PLAN.md).
