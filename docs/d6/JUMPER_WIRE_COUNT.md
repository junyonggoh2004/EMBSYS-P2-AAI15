# D6 jumper-wire count for the three breadboards

Recorded 16 November 2026 from the user's supplied wire-count breakdown.
Use with the [pin-to-hole wiring guide](PIN_TO_HOLE_WIRING.md) and the
[integration handoff](D6_INTEGRATION_HANDOFF.md). These are **planning counts**,
not proof that the proposed Sonar B and radar GPIOs have been approved.

## Counting assumption

Each ESP32-S3 and module sits **outside** its breadboard and exposes male
header pins. Each listed connection from a male pin to a breadboard hole uses
one **male-to-female** jumper: the female end attaches to the header and the
male end enters the breadboard. Power rails are continuous within the chosen
section. Under these assumptions the diagrams need no female-to-female or
male-to-male jumpers.

| Board | Male-to-female | Female-to-female | Male-to-male |
|---|---:|---:|---:|
| A: sensors and radar | 31 | 0 | 0 |
| B: SPI flash | 12 | 0 | 0 |
| C: LCD | 15 | 0 | 0 |
| **Total** | **58** | **0** | **0** |

Purchase or gather **about 70 male-to-female leads** to allow for broken
leads, replacements and layout changes. This is a shopping allowance, not a
claim that all 70 are used simultaneously.

## Board A: 31 leads

| Component | Breadboard connections | Male-to-female leads |
|---|---|---:|
| AHT temperature/humidity | VCC, GND, SDA, SCL | 4 |
| TSL2561 light sensor | VCC, GND, SDA, SCL | 4 |
| Sonar A | VCC, GND, TRIG, ECHO | 4 |
| Sonar B | VCC, GND, TRIG, ECHO | 4 |
| LD2450 radar | 5V, GND, TX, RX | 4 |
| ESP32-S3 Super Mini | GND, 3V3, 5V and eight signals: GPIO1, 2, 4, 5, 6, 7, 17, 18 | 11 |
| **Board A total** | | **31** |

The AHT and TSL2561 share the **two ESP32 I2C signal wires** at GPIO1 and
GPIO2; each sensor still needs its own SDA/SCL lead to the shared breadboard
rows. If the initial radar test only connects radar **TX -> ESP RX**, omit
the radar RX lead and the ESP UART TX lead: **29** for A and **56** overall.
The exact radar module may need a soldered header/adapter; a bare PCB pad is
not a male header. GPIO17/18 on the HW-747 Super Mini are underside pads and
also need reliable access. Confirm those physical connectors before buying
jumper leads solely from the count.

## Board B: 12 leads

| Component | Breadboard connections | Male-to-female leads |
|---|---|---:|
| SPI flash module | VCC, GND, CS, CLK, DI, DO | 6 |
| ESP32-S3 Super Mini | GND, 3V3, GPIO10 CS, GPIO11 SCK, GPIO12 MOSI, GPIO13 MISO | 6 |
| **Board B total** | | **12** |

The later D6 SPI device-swap demonstration may require leads for a second
device. It is not counted until that device and its connector are selected.

## Board C: 15 leads

| Component | Breadboard connections | Male-to-female leads |
|---|---|---:|
| LCD | VCC, GND, SCL/SCK, SDA/MOSI, RST, DC, CS, BL | 8 |
| ESP32-S3 Super Mini | GND, 3V3, GPIO12 SCK, GPIO11 MOSI, GPIO8 RST, GPIO9 DC, GPIO10 CS | 7 |
| **Board C total** | | **15** |

The LCD's BL-to-GND arrangement is specific to the tested module.

## Adjust before wiring or purchasing

- A split breadboard rail needs **one additional male-to-male jumper for each
  rail break you choose to bridge**. Never bridge the 5V and 3V3 rails.
- The Grove light-sensor cable may require an adapter instead of four ordinary
  female jumper ends; inspect its cable ends and breakout pins first.
- If a board or module presents female sockets, solder pads or a keyed
  connector instead of male header pins, revise the lead type and count for
  that item. Do not assume wire color identifies a pin.
- Sonar B GPIO6/7 and radar UART GPIO17/18 remain **proposed** in the D6
  diagrams. Their approval can change the final layout, although the number
  of signal leads would usually stay the same.
