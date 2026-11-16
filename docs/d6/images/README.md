# D6 board diagrams

The active images now depict the photographed ESP32-S3 N16R8 setup:
Boards A/B use green screw-terminal expansion plates; Board C uses a bare
board with male headers. See [board identification and source photos](../N16R8_BOARD_SETUP.md).
The old Super Mini test evidence remains historical.

The six schematic PNGs were first prepared for the integration handoff and
were redrawn on 16 November for the photographed N16R8 board layout.
They show breadboard-connected terminal rows and separate power rails:

- [Board A inputs](board-a-inputs.png): AHT, TSL2561, Sonar A/B and LD2450.
  Purple rows are **proposed**, not approved D6 GPIO assignments.
- [Board B processing and flash](board-b-processing-flash.png): the previously
  tested D2 flash pin map, not a completed D6 storage run.
- [Board C LCD](board-c-lcd.png): the ST7789 pin map previously verified on the Super Mini; the N16R8 C
  and live B-to-C display still need tests.

Read [the handoff](../D6_INTEGRATION_HANDOFF.md) for the connection status,
power notes, materials and staged test plan. Check module markings and actual
board pin labels before wiring.

The [drawing script](draw_board_diagrams.py) reproduces these images using
Pillow and Arial on Windows. Its source was moved from the `lcd-output`
workspace's generated `output/d6-wiring-20261113` folder.

The [detailed pin-to-hole guide](../PIN_TO_HOLE_WIRING.md) links the newer [A](board-a-detailed.png), [B](board-b-detailed.png) and [C](board-c-detailed.png) diagrams styled like the prior LCD/flash breadboard sketch. [Drawing source](draw_pin_to_hole_diagrams.py).
