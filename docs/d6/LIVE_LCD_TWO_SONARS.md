# Live Board C LCD: separate Sonar A and B distances

The landscape room monitor now has two lines in its distance card: `A` and `B`, both in cm. The new `v=3` snapshot carries `distance_a_cm` and `distance_b_cm` independently. A value of `-1` means no recent valid echo and displays as `--`; it is never shown as zero distance. Board C still accepts the older v1 and v2 mock snapshots. In v2, the single distance appears on A and B shows `--`.

**Flashed and observed on 24 Nov 2026:** Board A samples the AHT, TSL2561, and both sonars. Board B decodes their MQTT frames and publishes a v3 snapshot to `d6/board-b/snapshot` about every two seconds while AHT and light are fresh. C validates and draws it. Board C was flashed as `s3-003` on COM13, connected to the hotspot broker, and displayed live values after `D6VIEW`; its console accepted snapshots through sequence 317, well beyond ten seconds. Board B independently publishes live `comfort-summer` scores on `d6/board-b/comfort`. The laptop broker and private Wi-Fi still need to stay on, but the laptop relay is no longer part of the new data path.

B sends `-1` for either sonar without a valid echo in the last six seconds. AHT and light must both have recent valid readings or no snapshot is sent. C clears old environmental and sonar values and labels the snapshot **STALE** after six seconds without an update. `occupancy:0` is a schema placeholder; C displays `OCC --`, not a measured room count. The doorway thresholds and timed entry/exit logic still need physical calibration and a Board A event to Board B occupancy path. SPI flash persistence is also a separate step.

## Run after flashing updated Board B and C firmware

Configure A and B with [`config/board_a.cfg`](../../config/board_a.cfg) and [`config/board_b_ml.cfg`](../../config/board_b_ml.cfg) using [`ML_commands.md`](../../ML_commands.md). Keep all three boards and the laptop on the private 2.4 GHz Wi-Fi, with the Mosquitto broker running. Select `comfort-summer` on B if you want the live comfort card.

In one normal PowerShell window from the repository root:

```powershell
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-003/cmd' -m 'D6VIEW'
```

In a second normal PowerShell window, observe B's snapshot topic. Leave this window running for a persistent display check:

```powershell
& "$env:ProgramFiles\mosquitto\mosquitto_sub.exe" -h 127.0.0.1 -t 'd6/board-b/snapshot' -v
```

The subscriber should continue receiving fresh snapshots beyond ten seconds. `-1` means that sonar has no recent valid reading. Press Ctrl+C to stop monitoring. Run `D6VIEW` again after resetting C or running `LCDTEST`. A live C console reports `C snapshot ... display=0 active=1`. On 24 Nov, the team confirmed the horizontal LCD showed the live values and C accepted sequences 276–317 while active. If A or B stops updating for six seconds, C should show `STALE` and clear the old values; this disconnect test has not been observed on hardware. Do not run the old laptop relay at the same time as B's publisher; both use the same topic and sequence space.

The v3 payload format is ordered as `v`, `seq`, `occupancy`, `distance_a_cm`, `distance_b_cm`, `temperature_c`, `humidity_pct`, `light_lux`, `source`. C rejects malformed, out-of-range, duplicate, or out-of-order snapshots. The A and B fields are separate samples; they are not added or averaged.
