# Current D6 doorway use case: two sonars, no radar

**Decision recorded 21 November 2026.** The user removed the LD2450 radar from
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

Board A samples the two sonars **sequentially** to avoid acoustic overlap and
the single pulse-capture resource conflict. For the proposed crossing logic,
the first qualifying sonar detection starts a bounded crossing interval. The
other sonar must then detect the target in the expected spatial sequence.
With Sonar A on the outside and B on the inside, A then B is an entry candidate
and B then A is an exit candidate; reverse those labels if the physical
placement is reversed. Clear -> A -> both -> B -> clear and its reverse are
the target sequences. A single detection, a turnaround, no Echo, ambiguous
overlap or an expired interval must not become a confirmed count. Thresholds,
hysteresis, crossing interval, clear/rearm time and multi-person policy require
measured doorway trials and team review; no arbitrary numeric defaults are
accepted as the final use-case contract.

A publishes identified environmental frames and, once implemented, one bounded
crossing candidate per completed sequence with node/boot identity, sequence
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

The existing D3 `WINDOW` command still **requires a radar UART source** and
must not be presented as the sonar-only trigger. An automatic first-sonar
crossing state machine, A-to-B candidate transport, B-only occupancy handling,
final flash writes and a live B-to-C display are separate integration work.
The existing B-to-C synthetic MQTT screen is a mock test, not evidence that
real Board A doorway events reach the LCD. A raw distance is not occupancy.

## Current evidence and next gates

The [19 November record](BOARD_A_BRINGUP_20261119.md) reports AHT and light
passing, Sonar A passing on direct GPIO10/11 signal leads, and Sonar B passing
once on direct GPIO6/7 leads before losing power during later rewiring. It
does **not** show valid simultaneous distances from both sonars or a completed
doorway crossing. Sonar B's 3V3 and GND feeds must be verified, then both
sonars retested separately and with `PAIR` in both orders. Do not infer
hardware success from a config ACK or from a `PAIR` request alone.

1. Confirm Board A's native USB console, actual wiring and correct flashed
   firmware. Run the [Windows Board A test procedure](BOARD_A_SENSOR_TEST.md)
   with each sensor isolated and record raw results.
2. Restore both sonar power paths, obtain valid changing distances from each
   separately, then verify a sequential pair and its 50 ms ceiling. Include
   no Echo and stuck/loose-contact trials.
3. Implement the sonar-first crossing state machine with bounded timing and
   a single event per completed sequence; test entry, exit, lone approach,
   turnaround, loitering, close followers, ambiguous order and recovery.
4. Implement/verify A candidate -> broker -> B final decision/storage ->
   broker -> C live status. Record one B occupancy change per accepted unique
   crossing, no change on duplicate/fault/ambiguous events, and a stale LCD
   indication when updates stop.

The active [validation plan](../D6_USECASE_VALIDATION_PLAN.md) and
[worksheet](../D6_TRIAL_WORKSHEET.md) track these gates. The 13 November
[radar handoff](D6_INTEGRATION_HANDOFF.md) and
[archived doorway plan](reference/D6_USECASE_VALIDATION_PLAN.md) are retained
as history, not the current sensor inventory.
