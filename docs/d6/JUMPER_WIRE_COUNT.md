# D6 wire count for three bare N16R8 boards

**Current sonar-only Board A (21 November 2026):** The [current wiring guide](BOARD_A_SONAR_ONLY_WIRING.md) uses 4 ESP-to-breadboard leads (3V3, GND, SDA, SCL), 8 AHT/light module leads, 4 sonar power leads and 4 direct female-to-female sonar signal leads: **20 leads for A**, assuming the modules expose matching male headers. Sonar B power uses B4/B7 directly; no rail-feed jumpers are required. Board B remains 12 and C 15 leads in the earlier plans, for **47 total** under those assumptions. Grove adapters or later rail use can change lead types/counts. The radar-based 59-lead estimate below is retained as a historical proposal.

## Historical radar-based wire count (18 November)

Updated 18 November 2026. [Board identification](N16R8_BOARD_SETUP.md) and
[pin-to-hole wiring](PIN_TO_HOLE_WIRING.md) define the fixture. Boards A, B
and C have accessible male headers and sit outside their breadboards.

| Board | Module connections | ESP header connections | Other jumper | Total |
|---|---:|---:|---:|---:|
| A: sensors and radar | 20 | 11 | 1 B7-to-GND-rail bridge | 32 |
| B: SPI flash | 6 | 6 | 0 | 12 |
| C: LCD | 8 | 7 | 0 | 15 |
| **Total** | **34** | **24** | **1** | **59** |

If each ESP32 and module exposes male pins and the rails are continuous,
the plan uses **58 male-to-female jumpers plus one male-to-male ground-rail
bridge**. Allow about **70 male-to-female leads** for replacements and layout
changes. Lead types still depend on the Grove cable and radar connector.
Any split rail may require another male-to-male bridge within the same
voltage rail; never bridge 3V3 and 5V.

## Board A: 32 connections

| Component | Connections | Lead count |
|---|---|---:|
| AHT temperature/humidity | VCC E4, GND E7, SDA E5, SCL E6 | 4, if male header |
| TSL2561 light sensor | VCC D4, GND D7, SDA D5, SCL D6 | 4, subject to Grove adapter |
| Sonar A | VCC C4, GND C7, TRIG E8, ECHO E9 | 4, if male header |
| Sonar B | VCC B4, GND rail N2, TRIG E10, ECHO E11 | 4, if male header |
| LD2450 radar | 5V rail R2, GND rail N3, TX E12, RX E13 | 4, subject to radar connector |
| Bare N16R8 ESP male header | 3V3 A4, GND A7, 5V rail R1, GPIO1/2/4/5/6/7/17/18 | 11 male-to-female |
| Row-7 ground bridge | B7 to blue GND rail N1 | 1 male-to-male |

AHT and light share GPIO1/2. Their addresses differ: AHT 0x38,
TSL2561 preset 0x29. For receive-only radar bring-up, omit radar RX E13
and ESP GPIO18 A13: **30 A connections**, **57 overall** including the
ground bridge. Sonar B GPIO6/7 and radar GPIO17/18 remain proposed pending
team approval; the N16R8 hardware has not yet passed all sensor checks.

## Board B: 12 connections

| Component | Connections | Lead count |
|---|---|---:|
| External SPI flash | VCC, GND, CS, CLK, DI, DO | 6, if male header |
| Bare N16R8 ESP male header | GND, 3V3, GPIO10/11/12/13 | 6 male-to-female |

The future second SPI device for the D6 swap demo is not included.

## Board C: 15 connections

| Component | Connections | Lead count |
|---|---|---:|
| ST7789 LCD | VCC, GND, SCL, SDA, RST, DC, CS, BL | 8, if male header |
| Bare N16R8 ESP male header | GND, 3V3, GPIO12/11/8/9/10 | 7 male-to-female |

BL-to-GND is specific to the tested active-low LCD module. The same GPIO
map still needs an isolated test on this N16R8 Board C.

## Connector and rail checks

- Follow each ESP32's **printed header pin label**. A male-to-female jumper
  connects that header to a breadboard hole. Do not infer pins from wire
  colors or from another board's orientation.
- The Board A B7-to-N1 bridge is required to carry row-7 GND to the
  continuous blue ground rail. Bridge a split power rail only within the
  **same voltage** rail, if needed. Never join 5V to 3V3. Check continuity with USB unplugged.
- Grove and radar connectors can change the lead types. Follow module
  labels and verified voltage ratings, not wire colors.

## Alternative: spacious 65-row Board A

The table above is the earlier compact proposal and is retained. The [spacious layout](BOARD_A_SPACED_65_ROW_LAYOUT.md) has the same 20 sensor-module connections and 11 ESP header connections. It adds three male-to-male bridges: B4->3V3 top rail P1, B7->GND top rail N1, and top-to-bottom blue GND N3->N4. That makes **34 Board A leads** (31 male-to-female plus 3 male-to-male), or **61 leads across A/B/C** (58 male-to-female plus 3 male-to-male). Omit radar RX and ESP GPIO18 to reduce the spacious totals to 32 on A and 59 overall. Use the actual connector ends to choose lead types; these counts assume male module and ESP header pins. A continuous GND rail makes the N3->N4 bridge electrically redundant, but keeping it on the plan is safe.
