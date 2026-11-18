# N16R8 boards selected for D6

Updated 17 November 2026 after the user removed the expansion boards. Boards
**A, B and C** are bare ESP32-S3 N16R8 development boards with accessible male
headers. Each ESP32 and its modules sit outside a separate breadboard. Use a
male-to-female jumper from each ESP32 header pin to its named breadboard hole.
Follow the **printed pin labels on the actual board**; N16R8 identifies memory
capacity, not a unique PCB pinout.

| Board | ESP connection to its breadboard |
|---|---|
| A: inputs | Male header pins for GND, 3V3, USB-backed 5V, GPIO1/2, GPIO4/5, and proposed GPIO6/7/17/18 |
| B: processing/storage | Male header pins for GND, 3V3, GPIO10/11/12/13 |
| C: LCD | Male header pins for GND, 3V3, GPIO12/11/8/9/10 |

The [bare board front](images/n16r8-c-front.jpg) and [back](images/n16r8-c-back.jpg)
photos show one of the N16R8 boards for orientation. Inspect A and B's own
printed labels before connecting them. The photographed labels resemble the
[Espressif DevKitC-1 J1/J3 pin tables](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html#header-block),
but the boards are third-party; the photos and Espressif table are guides,
not proof of every board's wiring or current limit.

GPIO17/18 are present on the photographed N16R8 header. Their use for radar
UART is **proposed**, pending the team pin and firmware review. If approved,
connect radar TX to the ESP **GPIO17** header pin (UART RX in this plan), and
radar RX to **GPIO18** (UART TX) only when needed. Do not substitute header
pins labelled `TX` and `RX`: on the DevKitC-style map those are GPIO43/44.

## Power and USB checks

- With USB unplugged, identify each ESP32's `3V3`, `5V`, `GND` and GPIO labels.
  Verify the jumper really starts at that labelled header pin. Check that the
  chosen 5V and 3V3 breadboard rail sections are isolated and that every
  device power lead reaches a continuous section of the intended rail.
- Board A's USB-backed `5V` pin is proposed for the LD2450 only. Measure 5V
  and 3V3 under load and check the board/USB current budget. Share ground;
  never connect 5V to GPIO or 3V3. Confirm the sonar Echo voltage before
  connecting a module; a 5V Echo requires level protection.
- Use the USB-C connector wired to the chip's **native USB** for this
  firmware's USB-Serial-JTAG console. The other connector may use a USB-UART
  bridge. COM numbers depend on the board and USB mode.
- Build each node with `BOARD=n16r8`. This selects octal PSRAM; the current
  firmware still uses a 4 MB partition layout within the physical 16 MB
  flash. The external W25QXX on B is a separate device.
- Earlier Super Mini LCD/SPI evidence established the **GPIO/module map**.
  Board B has now passed read-only external `SPIID` and `SPICHECK` on N16R8;
  Board C has passed `LCDTEST 76 284 82 18 00 0` on N16R8 with visible
  colour bands and `LCD READY`. These are isolated checks; live D6 integration
  is still pending.

On 17 November, the newly flashed Board A responded to `HELP`, `SHOW`, and
`TASKS` on COM4. With its 3V3 sensor rail connected and radar 5V disconnected,
the TSL2561 produced valid lux readings; the AHT reported communication
errors and Sonar A timed out. Sampling was stopped. Later isolated checks found Sonar B pulse data
with uncertain distance and no bytes on the LD2450 raw UART. AHT remained
unresponsive on a temporary GPIO8/9 circuit; one proposed 10 kHz test
could not run because COM4 disappeared. On 18 November the revised
Board A rows 4-9 were tested again: AHT and light passed, Sonar A still
timed out, and GPIO4-to-GPIO5 loopback passed. See
[Stage 1 bring-up](BOARD_A_STAGE1_BRINGUP_20261118.md). Inspect Sonar A
before a combined doorway trial.

The active [Board A pin-to-hole map](PIN_TO_HOLE_WIRING.md#board-a-aht-light-two-sonars-and-radar) now retains JY's V2 three-sensor rows 4-9, with Sonar B on rows 10-11 and radar UART on rows 12-13. Remove all old Board A LCD wires, including row 14 and its power/BL leads; Board C owns the LCD. This change preserves the historical working GPIO map but has not yet passed a full N16R8 sensor run. Older Super Mini wiring and tests in other project folders remain historical fixtures.

On 18 November, Board B was flashed as node `s3-002` and reached the native
USB console on COM11. GPIO12-to-GPIO13 SPI loopback passed 11/11 samples,
and the external flash initially returned `SPIID INVALID raw=000000`.
After correcting the Board B wiring, `SPIID` returned `EF4016`,
`SPICHECK` passed read-only, and `SPISCAN` found 94 non-FF bytes in sector
`3FF000`. See the [Board B bring-up record](BOARD_B_BRINGUP_20261118.md).
External flash reads are verified; D6 storage writes remain unimplemented
and no external flash write was made during this trial.

On 18 November, Board C was backed up and flashed as `s3-003` over CH343
COM12. The native USB console was COM13 after reset. `LCDTEST` returned
`result=0`, the user confirmed the expected visible pattern, and `TASKS`
reported `BenchmarkTask stack_free=1756 B`. See
[Board C bring-up](BOARD_C_BRINGUP_20261118.md).

## Optional spacious Board A layout

The original compact Board A pin-to-hole map above remains documented. On the user's 65-row board, the [spacious alternative](BOARD_A_SPACED_65_ROW_LAYOUT.md) retains JY's AHT/light/Sonar A rows 4-9 and the same ESP GPIO map while moving only Sonar B to rows 28/29 and radar to 50/51/54. It has not yet been wired or tested as a complete fixture.
