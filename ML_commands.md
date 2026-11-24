# Board B ML model commands

These commands use the D6 three-board ESP32-S3 N16R8 setup: Board A is `s3-001`, Board B is `s3-002`, and Board C is `s3-003`. Run them from the **D6-Integration** checkout after the broker and boards are online. The D6 live demo uses `sine-float`, `sine-int8`, `comfort-summer`, and `people-counter`. The firmware also bundles an autoencoder for separate D4 work; it is outside this demo and use case. Selecting another bundled model does not require another flash. Selections and sensor mappings are volatile and must be loaded after a reboot. [Start the broker, sensor bridges, and default model first](usecase_commands.md#5-run-all-four-ab-sensor-bridges--normal-powershell-window-3).

| Model | Actual model input shape | Board B use | Result |
| --- | --- | --- | --- |
| `sine-float` | 1 float | Live AHT temperature/humidity and light are combined into one input; default | One unitless sine number |
| `sine-int8` | 1 INT8 value | Same live source fields, quantized model | One unitless sine number |
| `comfort-summer` | 2 floats | Live AHT temperature and humidity | Comfort score 0–99 plus class score |
| `people-counter` | 31 INT8 values | One Sonar A binding fills a rolling 31-reading window on B | `in`, `none`, or `out` plus confidence |

The `people-counter` window is a **model-specific exception**: it has one typed sonar binding that fills 31 time slots. The general live setup remains limited to 16 bindings, and the other demo models retain their original shapes. `people-counter` classifications do not update occupancy or the LCD. The current B remote window is an experimental transport check: Board A's normal 2 Hz sonar feed takes roughly 15.5 seconds to fill it, while the model's source training used roughly 50 ms per sample. Validate preprocessing, timing and labelled crossings before treating a class as an actual direction. The 24 Nov live test did not produce a classification because Sonar A repeatedly timed out; the three other demo models returned live `status:OK` results.

## Common setup and result window — Windows

Use **normal PowerShell** for these commands. Start the Mosquitto broker using the Administrator PowerShell instructions in [usecase_commands.md](usecase_commands.md), connect all boards to the saved private 2.4 GHz hotspot, and confirm fresh `s3-001` and `s3-002` heartbeats. The firmware broker address must match the laptop's Wi-Fi IPv4 address. The local Mosquitto commands below connect through `127.0.0.1`.

**PowerShell window 1 — results.** Keep this command running; press **Ctrl+C** to return to its PowerShell prompt.

```powershell
Set-Location 'C:\Users\brend\EMBSYS-P2-AAI15-D6-Integration'
$sub = "$env:ProgramFiles\mosquitto\mosquitto_sub.exe"
& $sub -h 127.0.0.1 -t 'pico/s3-002/ml/status' -t 'pico/s3-002/ml/result' -t 'd6/board-b/comfort' -v
```

**PowerShell window 2 — commands.** If you have already run the full setup commands, keep its `$pub` variable and skip the first two lines. Otherwise load `config/board_a.cfg` on A and `config/board_b_ml.cfg` on B using the [startup guide](usecase_commands.md#5-run-all-four-ab-sensor-bridges--normal-powershell-window-3) before selecting a live model.

```powershell
Set-Location 'C:\Users\brend\EMBSYS-P2-AAI15-D6-Integration'
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"
```

A valid `ml_setup` publishes `{"ok":true,"reason":"applied",...}` on B's ML status topic; the next fresh result reports the selected `model`, `input_source`, and `status`. A rejected setup preserves the previous live model. Live model switches below do **not** require `STOP`.

## 1. `sine-float` — default live model

Configuration file: [`config/board_b_sine_float.json`](config/board_b_sine_float.json). It binds AHT temperature, AHT humidity and light from A. Firmware normalizes and combines these readings into the model's **single** input. The result is a unitless demonstration value, not a room count.

```powershell
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_sine_float.json'
```

Expect `"model":"sine-float"`, `"input_source":"remote:s3-001"`, and `"status":"OK"` when all three source fields are fresh. If you want the separate demonstration cutoff, send `ML THRESHOLD 0.5`; results then include `threshold_exceeded`. Values **greater than** 0.5 exceed the cutoff; 0.5 itself does not. Clear that optional threshold with `ML THRESHOLD OFF`.

```powershell
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'ML THRESHOLD 0.5'
# Later, remove the optional cutoff:
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'ML THRESHOLD OFF'
```

Replay file: [`config/ml_replay_sine_float.txt`](config/ml_replay_sine_float.txt). It supplies one model-ready input (`1.5707963`), independent of the sensors. Use the [replay procedure](#replay-a-model-ready-vector) below.

```powershell
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'STOP'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\ml_replay_sine_float.txt'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'RUN'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_sine_float.json'
```

## 2. `sine-int8` — live quantized sine model

Configuration file: [`config/board_b_sine_int8.json`](config/board_b_sine_int8.json). It uses the same three live fields and combines them into one model input. This version is INT8; its result is still a unitless sine demonstration.

```powershell
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_sine_int8.json'
```

Expect `"model":"sine-int8"`, `"input_source":"remote:s3-001"`, and `"status":"OK"`. Its optional `ML THRESHOLD 0.5` follows the same strict `> 0.5` rule as `sine-float`. Replay file: [`config/ml_replay_sine_int8.txt`](config/ml_replay_sine_int8.txt).

```powershell
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'STOP'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\ml_replay_sine_int8.txt'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'RUN'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_sine_int8.json'
```

## 3. `comfort-summer` — live AHT comfort score

Configuration file: [`config/board_b_comfort_summer.json`](config/board_b_comfort_summer.json). It binds A's typed AHT temperature and humidity to the model's two inputs.

```powershell
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_comfort_summer.json'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-003/cmd' -m 'D6VIEW'
```

Expect `"model":"comfort-summer"`, `"comfort_score":N`, `"class_score":...`, and `"status":"OK"` on B's result topic. B also sends a live, non-replay score on `d6/board-b/comfort`; C's landscape **LIVE COMFORT** card shows it and changes to **STALE** six seconds after updates stop. For the planned full LCD display, scores **80–99** will be labelled COMFORTABLE and scores below 80 UNCOMFORTABLE; those labels are **not yet implemented**. Replay file: [`config/ml_replay_comfort_summer.txt`](config/ml_replay_comfort_summer.txt), containing the synthetic `26.3 °C, 67% RH` example. Replay results do not refresh C's live comfort card.

```powershell
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'STOP'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\ml_replay_comfort_summer.txt'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'RUN'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_comfort_summer.json'
```

## 4. `people-counter` — experimental live 31-reading window

Configuration file: [`config/board_b_people_counter.json`](config/board_b_people_counter.json). B follows A's `SONAR_A` through its `A_SONAR_A` alias with `remote_profile=pulse`; **one** binding supplies successive readings to the 31-slot window. The existing [`config/board_b_ml.cfg`](config/board_b_ml.cfg) gives that alias a 500 ms expected interval.

The full setup starts A's two-sonar `DOORWAY` service. That service samples each sonar about every **140 ms**, so its cadence does not match B's current 500 ms window setting. For this isolated ML transport test, pause A's doorway service and return to the ordinary 2 Hz sonar profile before selecting the model:

```powershell
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-001/cmd' -m 'DOORWAY OFF'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-001/cmd' -m 'RUN'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_people_counter.json'
```

Watch for accepted ML status, then `"model":"people-counter"`, `"input_source":"remote:s3-001"`, `"direction":"in"|"none"|"out"`, and `class_score` after 31 consecutive valid A Sonar A frames. The class indices are **0=IN, 1=NONE, 2=OUT**. An invalid echo or receive gap clears the window. B currently uses receive time, not A's source timestamp, and does not deduplicate an otherwise valid repeated MQTT frame. Its 2 Hz cadence differs from the model's roughly 50 ms training cadence. This test proves the remote frame and 31-value inference path; it does **not** validate direction, change occupancy, or put the class on C's LCD.

Restore B's default model and A's doorway service after this isolated test:

```powershell
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_sine_float.json'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-001/cmd' -m 'DOORWAY SONAR_A SONAR_B 70 85 70 85 70 2000'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-001/cmd' -m 'RUN'
```

Replay file: [`config/ml_replay_people_counter.txt`](config/ml_replay_people_counter.txt). It sends 31 zeroes as a synthetic model-ready window. To test model execution without waiting for sensor frames:

```powershell
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'STOP'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'ML MODEL people-counter'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'ML OUTPUT publish'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\ml_replay_people_counter.txt'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'RUN'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_sine_float.json'
```

The replay result says `"input_source":"replay"`. It cannot establish entry or exit accuracy.

## Replay a model-ready vector

Each model section gives its complete replay commands and the matching live configuration to restore afterward. A replay result is labelled `"input_source":"replay"` and does not count as a live sensor test.

`STOP` pauses B's sensor scheduler but retains the selected ML model. The final live setup restores fresh-input processing. If you previously set `ML THRESHOLD`, note that a new `ml_setup` clears it.

## Mac / Linux command form

Use the same live configuration files from the repository root. Start the broker and A/B sensor aliases with the [Mac / Linux setup](usecase_commands.md#mac--linux). For any live JSON file named in sections 1–4:

```sh
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-002/cmd -f config/board_b_sine_float.json
# Replace the filename with the selected model's board_b_*.json file.
```

For replay, `STOP` B, choose `ML MODEL <name>` when needed, set `ML OUTPUT publish`, send the corresponding `config/ml_replay_*.txt` file with `-f`, then `RUN` and reapply the default live JSON. The people-counter live test also requires `STOP`, `DOORWAY OFF`, `RUN` on A as shown above, using `mosquitto_pub -m` for each command.

## End or reset the session

The default live model is restored by sending [`config/board_b_sine_float.json`](config/board_b_sine_float.json). To stop A and B at the end, send `STOP` to each, then press **Ctrl+C** in the subscriber and broker windows. The [full shutdown sequence](usecase_commands.md#7-end-the-session-1) also covers unplugging the boards. A reboot clears the runtime sensor mappings and model selection; repeat the full startup commands before a new model test.
