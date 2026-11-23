# Legacy LCD plus LD2450 breadboard prototype

Transferred from the uncommitted `lcd-output` working tree on 13 November
2026 so the older wiring discussion is not lost. This is **not the approved
D6 three-board fixture** and has no validated radar-to-LCD firmware path.
The diagram is [here](LCD_LD2450_BREADBOARD_legacy.svg).

The old proposal placed the LCD and radar beside one ESP32-S3. It retained
the isolated LCD pins and added:

| Connection | Old breadboard position | Meaning |
|---|---|---|
| Radar GND | Blue ground rail N4 | Common ground with ESP32 and LCD |
| External 5V supply GND | Blue ground rail N5 | Common UART/power reference |
| External 5V supply positive | Separate right red rail | Radar power only |
| Radar 5V | Same right red rail | Radar power only |
| ESP32 GPIO7 | A20 | UART receive input |
| Radar TX | E20 | Same row as ESP32 GPIO7 |

Radar RX was left disconnected for a receive-only test. The external-supply
arrangement and GPIO7 UART proposal are historical; the selected D6 plan
places the radar on **Board A**, the LCD on **Board C**, and proposes A's
USB-backed 5V pin for radar power after supply checks. Current proposed A
UART pins are GPIO17/18, pending team approval. Do **not** combine this
legacy prototype with the [current A diagram](../images/board-a-inputs.png).

The transferred [single-sonar command log](live-sonic-20261103.log) is a
short 3 November 2026 D3 command capture using GPIO1/2. It records commands
only, not a successful D6 doorway observation or Sonar A/B timing proof.
