# Live Board C LCD: separate Sonar A and B distances

The landscape room monitor now has two lines in its distance card: `A` and `B`, both in cm. The new `v=3` snapshot carries `distance_a_cm` and `distance_b_cm` independently. A value of `-1` means no recent valid echo and displays as `--`; it is never shown as zero distance. Board C still accepts the older v1 and v2 mock snapshots. In v2, the single distance appears on A and B shows `--`.

This is an **interim live display**. Board A samples the AHT, TSL2561, and both sonars. Board B receives their MQTT frames. The laptop relay [`tools/d6_live_lcd_relay.py`](../../tools/d6_live_lcd_relay.py) decodes B's relayed copies and publishes a v3 snapshot to `d6/board-b/snapshot` about every two seconds. C validates and draws it. Board B independently publishes live `comfort-summer` scores on `d6/board-b/comfort`. The relay runs until Ctrl+C; the laptop and private broker must stay on. Board B does not yet publish this full snapshot itself.

The relay sends `-1` for either sonar without a valid echo in the last six seconds. AHT and light must both have recent valid readings or no snapshot is sent. `occupancy:0` is a **placeholder**, not a measured room count. The display's `LIVE` label applies to the environmental and sonar readings, not to occupancy. The doorway thresholds and timed entry/exit logic still need physical calibration and a Board A event to Board B occupancy path. SPI flash persistence is also a separate step.

## Run after flashing the updated Board C firmware

Configure A and B with [`config/board_a.cfg`](../../config/board_a.cfg) and [`config/board_b_ml.cfg`](../../config/board_b_ml.cfg) using [`ML_commands.md`](../../ML_commands.md). Keep all three boards and the laptop on the private 2.4 GHz Wi-Fi, with the Mosquitto broker running. Select `comfort-summer` on B if you want the live comfort card.

In one normal PowerShell window from the repository root:

```powershell
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-003/cmd' -m 'D6VIEW'
```

In a second normal PowerShell window, also from the repository root, keep this command running:

```powershell
python -B .\tools\d6_live_lcd_relay.py
```

The relay prints both values separately; `--` means that sonar has no recent valid reading. For a timed check, add `--seconds 30`. Press Ctrl+C to stop. Run `D6VIEW` again after resetting C or running `LCDTEST`. A live C console reports `C snapshot ... display=0 active=1`. Check the physical LCD too: the code and host tests establish the parser/layout, while the two-line display still needs a fresh visual hardware confirmation after flashing this firmware.

The v3 payload format is ordered as `v`, `seq`, `occupancy`, `distance_a_cm`, `distance_b_cm`, `temperature_c`, `humidity_pct`, `light_lux`, `source`. C rejects malformed, out-of-range, duplicate, or out-of-order snapshots. The A and B fields are separate samples; they are not added or averaged.
