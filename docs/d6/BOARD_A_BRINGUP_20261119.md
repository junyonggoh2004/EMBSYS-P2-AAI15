# Board A bring-up: 19 November 2026

Board A tested by Hasif on the N16R8 with Brendan's [spaced 65-row layout](BOARD_A_SPACED_65_ROW_LAYOUT.md), firmware `D6-Integration` at `09820c5` plus the sonar change below, built with `BOARD=n16r8` as node `s3-001`. Brendan to review and amend.

## Result

| Item | Status |
|---|---|
| AHT and TSL2561 | Pass: 24.98 to 25.45 C, about 63 % RH, 172 to 355 lux |
| Sonar A | Pass on GPIO10 TRIG and GPIO11 ECHO wired straight to the header pins |
| Sonar B | Passed once on GPIO6/7 (5.2 to 14.4 cm), then lost power during the radar rewiring; to debug |
| Two sonars polled together | `-6` fixed: both report every cycle (firmware change below). Valid distances from both at the same time not yet seen, because Sonar B was down whenever Sonar A worked |
| MQTT | Pass on the hotspot's 2.4 GHz band: config over MQTT, readings published, commands answered ([logs](../../parity/logs/mtk/d6/board-a-20261119/)) |
| LD2450 radar | Dropped from Board A: silent on two boards (see below) |

## Changes in this commit

**Two sonars at once** ([gpio.c](../../libraries/EdgeCore/src/bus/gpio.c)). The board captures one echo at a time (D3-2), so a second periodic sonar that fell due while the first was measuring failed with `-6` every cycle. It now waits: it gives the first sonar's ping 30 ms to die away, then measures, and reports `-6` only if the capture is still held after 100 ms. `PAIR` and `WINDOW` are unchanged. The pulse test in `tools/cody_test.sh` covers the wait, the guard and the limit.

**Wrong USB port** ([mtk.sh](../../tools/mtk.sh)). The N16R8's second USB-C port is a CH343 bridge (VID 0x1A86) that shows boot messages only. `tools/mtk.sh` now says so when only that port is found, or when `PORT` names it, instead of reporting no board.

**Board A config** ([board_a.cfg](../../config/board_a.cfg)): AHT and light on GPIO1/2, Sonar A on GPIO10/11, Sonar B on GPIO6/7, both sonars at 2 Hz with a pull-down on ECHO so a disconnected echo line times out instead of reading noise as short pulses.

## Wiring as tested

AHT and light stay on Jun Yong's rows 4 to 7 (3V3 at A4, SDA GPIO1 at A5, SCL GPIO2 at A6, GND at A7).

| Device | VCC | GND | TRIG / TX | ECHO / RX |
|---|---|---|---|---|
| Sonar A | C4 | C7 | GPIO10, direct to the header pin | GPIO11, direct |
| Sonar B | red + strip, fed by B4 | blue - strip, fed by B7 | GPIO6, direct | GPIO7, direct |

GPIO4 and GPIO5 were already in use on this board, so Sonar A moved to GPIO10/11. Signals now go straight from the sonar to the ESP header with female-to-female leads; power still comes from the breadboard.

## What was wrong

1. **Rail feeds missing.** The spaced layout powers Sonar B and the radar from the + and - strips, fed by jumpers B4 to + and B7 to -. B7 was not fitted, and B4 was reported missing when Sonar B was rewired, so the strips had no supply.
2. **Sonar signals through the breadboard.** Confirmed for Sonar B: with VCC and GND straight on the ESP's 3V3 and GND pins and TRIG/ECHO still in E28/E29, ECHO read as floating (it followed the pin's own pull-up and pull-down); with TRIG/ECHO moved to the header pins it read 5.4 cm. The ESP side was fine: a jumper from row 28 to row 29, and from row 8 to row 9, returned a 1,000 us trigger as about 1,020 us. Sonar A also floated on E8/E9 and read correctly direct (on a second N16R8, then on Board A), but its power and signal paths were not separated. The faulty lead or contact was not identified, so both sonars' signals were moved to the header pins.
3. **Two periodic sonars** returned `-6` (fixed above).
4. **MQTT disconnected** because the iPhone hotspot was on 5 GHz (channel 149). With Maximize Compatibility on and the laptop rejoined it moved to channel 6 and the board connected.
5. **Console on the wrong port** (the CH343 port), see above.

## Sonar B, to debug

After the radar work Sonar B stopped firing. GPIO6 and GPIO7 both read 0 even with the pull-up on, which is how an unpowered module loads its pins (the radar showed the same). The same wiring gave valid distances earlier on 19 November, so check its VCC lead in the + strip next to the B4 feed, its GND lead in the - strip next to the B7 feed, and that the B4 feed is still in place. Swapping the two sonar modules on their leads separates a lead fault from a module fault.

## LD2450 radar

No byte was ever received. On Board A: 3.3 V, 3.3 V and then 5 V through row 54, and 5 V straight from the header; at 256000 baud with TX/RX both ways, and at 115200, 57600 and 9600 baud on 3.3 V. On a second N16R8: 5 V straight from the header, then with both USB ports connected, at 256000 baud both ways. The radar TX line (GPIO17) was never driven high, and GPIO18 was held low throughout. A powered LD2450 holds TX high when idle, so the module is not starting: it is faulty or its 1.25 mm connector is not making contact. Measure 5 V across the radar's own 5V and GND pins before reusing it. Without the radar, `WINDOW` cannot open; counting with the two sonars alone needs a different trigger.

Never wire anything to GPIO35, 36 or 37 on these boards: they are the octal PSRAM lines.

## Open items

- Sonar B power (above), then rerun the two-sonar and `PAIR` checks.
- `tools/mtk_runtime_test.sh` stopped building at `9bcad1a` (B to C MQTT display fixture): `lcd_output_fill` and `lcd_output_draw_text` have no stubs in `tests/test_mtk_application_runtime.c`, and its `sizeof(bench_msg_t) == 40` check fails since the D6 fields were added. This change does not touch it.
- Decide the D6 trigger now that the radar is out.

## Live test from the CLI

One board on the native USB port, the hotspot on 2.4 GHz with the laptop on it, and the broker running (`/opt/homebrew/sbin/mosquitto -c ~/mosq.conf -v`). Replace the broker address with the laptop's (`ipconfig getifaddr en0`).

Terminal 1, console; wait for `[MQTT] Status: Connected`:

```sh
PORT=/dev/cu.usbmodem1101 sh tools/mtk.sh monitor
```

Terminal 2, everything the board publishes:

```sh
mosquitto_sub -h 172.20.10.5 -t 'pico/s3-001/#' -v
```

Terminal 3, load and run Board A over MQTT:

```sh
mosquitto_pub -h 172.20.10.5 -t pico/s3-001/cmd -m STOP
mosquitto_pub -h 172.20.10.5 -t pico/s3-001/cmd -m CLEAR
mosquitto_pub -h 172.20.10.5 -t pico/s3-001/config -f config/board_a.cfg
mosquitto_pub -h 172.20.10.5 -t pico/s3-001/cmd -m RUN
```

Terminal 2 shows `OK:AHT`, `OK:LIGHT`, `OK:SONAR_A` and `OK:SONAR_B` on `pico/s3-001/status/config`, then lines on `pico/s3-001/sensor/<name>/line`; terminal 1 shows the decoded readings. Move a hand in front of each sonar, cover the light sensor and breathe on the AHT to see the values change. The sonar pair, then stop:

```sh
mosquitto_pub -h 172.20.10.5 -t pico/s3-001/cmd -m STOP
mosquitto_pub -h 172.20.10.5 -t pico/s3-001/cmd -m "PAIR SONAR_A SONAR_B A 5000"
mosquitto_pub -h 172.20.10.5 -t pico/s3-001/cmd -m STOP
```

`[PAIR] state=complete outcome=0` with a width for each sonar is a pass. Without MQTT, paste the four lines of `config/board_a.cfg` into terminal 1 and type `RUN`.
