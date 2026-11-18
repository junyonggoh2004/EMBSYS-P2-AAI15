# D6 wire count for the photographed N16R8 setup

Updated 16 November 2026. [Board identification and photos](N16R8_BOARD_SETUP.md)
and [pin-to-hole wiring](PIN_TO_HOLE_WIRING.md) define the fixture.
Boards A and B use green screw-terminal expansion plates. Board C is bare
with male header pins. All three assemblies stay outside their breadboards.

| Board | Module-to-breadboard male-to-female leads | ESP connection leads | Total connections |
|---|---:|---:|---:|
| A: sensors and radar | 20 | 11 terminal-to-breadboard | 31 |
| B: SPI flash | 6 | 6 terminal-to-breadboard | 12 |
| C: LCD | 8 | 7 male-to-female from bare ESP header | 15 |
| **Total** | **34** | **17 terminal leads + 7 male-to-female** | **58** |

Thus the photographed layout calls for **41 male-to-female leads** and
**17 prepared insulated screw-terminal-to-breadboard leads**, if every
module exposes male pins and every listed ESP signal uses the plate's screw
terminal. The 58 figure is the number of connections, **not 58 identical
jumper leads**. The former about-70 male-to-female shopping estimate only
applied when all three ESP boards had accessible male headers and none used
the expansion plates. Check the actual cable ends before purchasing.

## Board A: 31 connections

| Component | Connections | Lead count |
|---|---|---:|
| AHT temperature/humidity | VCC, GND, SDA, SCL | 4 male-to-female, if male header |
| TSL2561 light sensor | VCC, GND, SDA, SCL | 4, subject to Grove adapter |
| Sonar A | VCC, GND, TRIG, ECHO | 4 male-to-female, if male header |
| Sonar B | VCC, GND, TRIG, ECHO | 4 male-to-female, if male header |
| LD2450 radar | 5V, GND, TX, RX | 4, subject to radar connector |
| N16R8 ESP via green plate | GND, 3V3, 5V, GPIO1/2/4/5/6/7/17/18 | 11 terminal-to-breadboard |

AHT and light share the ESP's two I2C signal connections at GPIO1/2;
each sensor still has its own SDA/SCL lead. For receive-only radar bring-up,
omit radar RX and ESP UART TX: **29 A connections**, **56 overall**.
The radar may require a dedicated connector or soldered leads. Sonar B
GPIO6/7 and radar GPIO17/18 remain proposed, pending team approval.

## Board B: 12 connections

| Component | Connections | Lead count |
|---|---|---:|
| External SPI flash | VCC, GND, CS, CLK, DI, DO | 6 male-to-female, if male header |
| N16R8 ESP via green plate | GND, 3V3, GPIO10/11/12/13 | 6 terminal-to-breadboard |

The future second SPI device for the D6 swap demo is not included.

## Board C: 15 connections

| Component | Connections | Lead count |
|---|---|---:|
| ST7789 LCD | VCC, GND, SCL, SDA, RST, DC, CS, BL | 8 male-to-female, if male header |
| Bare N16R8 ESP male headers | GND, 3V3, GPIO12/11/8/9/10 | 7 male-to-female |

BL-to-GND is specific to the tested active-low LCD module. The same GPIO
map still needs an isolated test on this N16R8 Board C.

## Connector and rail checks

- A/B screw terminals take a suitably stripped, secured insulated wire end;
  the breadboard end must fit its hole. Do not count an unmodified female
  Dupont socket as a screw-terminal connection. Verify the plate's printed
  labels map to the inserted board pins before energizing it.
- If the green plate exposes a reliable alternative header connection and
  you choose it, recalculate the 17 ESP lead types for that layout.
- Bridge each split breadboard power rail deliberately with a suitable
  male-to-male lead, if needed. Never join 5V to 3V3.
- Grove and radar connectors can change the lead types. Follow module
  labels and verified voltage ratings, not wire colors.
