# N16R8 boards selected for D6

Updated 16 November 2026 from the user's three board photos. The two
ESP32-S3 N16R8 boards for **A and B** sit in green screw-terminal expansion
plates. The bare board with exposed male pins is **C**. All three are
DevKitC-style 44-pin boards with two USB-C connectors; the photographed
modules/boards are third-party, so use the **printed pin labels on each
actual board and plate** as the final physical reference. N16R8 denotes
16 MB flash and 8 MB octal PSRAM; it does not uniquely identify a PCB pinout.

| Board | Photo | ESP connection to its breadboard |
|---|---|---|
| A and B | [Board seated in expansion plate](images/n16r8-a-b-expansion.jpg) | Matching labelled green screw terminals to breadboard holes using prepared insulated leads |
| C | [Bare front](images/n16r8-c-front.jpg), [bare back](images/n16r8-c-back.jpg) | Male header to breadboard hole using male-to-female jumper |

The photographed header labels resemble the [Espressif DevKitC-1 J1/J3
pin tables](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html#header-block).
This is a visual mapping aid, **not proof that the third-party board and
expansion plate have the same internal circuit or current limits**.
GPIO1/2, 4/5, 6/7, 8-13 and 17/18 are labelled on the boards; GPIO17/18
are accessible at the side, unlike the older HW-747 Super Mini. The radar
assignment GPIO17 as ESP RX and GPIO18 as ESP TX remains a software-routed
proposal and needs matching UART setup and team pin review. Use the
plate terminals physically marked **17** and **18** for that proposal.
The separate terminals marked `TX` and `RX` correspond to GPIO43 and
GPIO44 in the DevKitC-style pin map; they are **not** GPIO17/18. Verify
the actual plate-to-board continuity before connecting the radar.

## Power and USB checks

- On A/B, compare each green terminal's printed `3V3`, `5V`, `GND` and GPIO
  label with the inserted board label. With USB unplugged, verify continuity
  from the intended terminal to the matching board pin. Check that 5V and
  3V3 are isolated.
- Board A's USB-backed `5V` is proposed for LD2450 power only. Measure 5V
  and 3V3 under load and check the board/USB current budget. Share ground.
  Keep 5V off GPIO and 3V3. Confirm sonar Echo voltage before connecting.
- Use the USB-C connector wired to the chip's **native USB** for this
  firmware's USB-Serial-JTAG console. The other connector may be through a
  USB-UART bridge; identify the ports on the actual board. COM numbers can
  differ from the old COM5 fixture.
- Build each node with `BOARD=n16r8`. The existing preset selects octal
  PSRAM; the current firmware still uses the base 4 MB partition layout
  within the N16R8's 16 MB flash. Do not assume the extra flash is allocated
  to application storage. The external W25QXX on B is a separate device.
- Prior Super Mini LCD and SPI evidence establishes the **GPIO/module
  mapping**, not an electrical or runtime pass on these N16R8 boards. Run
  isolated LCDTEST on C and read-only SPIID on B before combined D6 trials.

The older Super Mini diagrams, photos and test evidence in other project
folders remain historical references for the earlier fixture. The active
three-board D6 wiring is [here](PIN_TO_HOLE_WIRING.md).
