# D6 board diagrams

The active images depict three bare ESP32-S3 N16R8 boards with accessible
male headers, each outside its breadboard. See [board identification and source photos](../N16R8_BOARD_SETUP.md).
The old Super Mini test evidence remains historical.

The six schematic PNGs were first prepared for the integration handoff.
Board A's two images were regenerated on 18 November from JY's Version 2
three-sensor rows 4-9, with new Sonar B/radar rows on the bare N16R8.
Board B's two diagrams retain their pin map and now show the 18 November
read-only SPI pass. Board C diagrams retain their pin map and now show the 18 November
N16R8 isolated LCD pass.
They show breadboard-connected terminal rows and separate power rails:

- [Board A inputs](board-a-inputs.png): AHT, TSL2561, Sonar A/B and LD2450.
  Rows 4-9 retain JY's V2 three-sensor fixture; purple rows 10-13 are
  **proposed** added D6 GPIO assignments. Board A's former LCD wiring
  is removed; the LCD is on C.
- [Board B processing and flash](board-b-processing-flash.png): the previously
  tested D2 flash pin map, now read-only verified on N16R8 B; D6 storage
  writes are still pending.
- [Board C LCD](board-c-lcd.png): the ST7789 pin map passed the isolated
  N16R8 C test with visible colours and `LCD READY`; live B-to-C display
  still needs implementation and testing.

Read [the handoff](../D6_INTEGRATION_HANDOFF.md) for the connection status,
power notes, materials and staged test plan. Check module markings and actual
board pin labels before wiring.

The [drawing script](draw_board_diagrams.py) reproduces these images using
Pillow and Arial on Windows. Its source was moved from the `lcd-output`
workspace's generated `output/d6-wiring-20261113` folder.

The [detailed pin-to-hole guide](../PIN_TO_HOLE_WIRING.md) links the newer [A](board-a-detailed.png), [B](board-b-detailed.png) and [C](board-c-detailed.png) diagrams styled like the prior LCD/flash breadboard sketch. [Drawing source](draw_pin_to_hole_diagrams.py).

- [Room monitor mock reference](room-monitor-mock-reference.png): user-supplied
  landscape target for future live Board C presentation. See
  [display reference](../ROOM_MONITOR_REFERENCE.md).

- [Spacious Board A](board-a-spaced.png): additional 65-row option that preserves JY V2 rows 4-9 and spreads Sonar B and radar. [Drawing source](draw_board_a_spaced.py). The earlier Board A diagrams remain as compact-layout references. The [dated physical results](../BOARD_A_STAGE1_BRINGUP_20261118.md#later-board-a-isolation-checks-on-18-november) show that the drawn power-rail routes are not yet validated.
