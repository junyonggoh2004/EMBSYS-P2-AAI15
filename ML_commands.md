# Board B ML commands

Use these commands from the repository root with the existing merged firmware: Board A is `s3-001`, Board B is `s3-002`, and both are connected to the laptop's MQTT broker. The five bundled models are already in the firmware; changing between them does not require a flash. The runtime setup is volatile, so repeat the startup block after a board reboot. `sine-float` is the default in [usecase_commands.md](usecase_commands.md).

| Model | Input | Use in this setup |
| --- | --- | --- |
| `sine-float` | A's AHT temperature and humidity, and light; averaged into one input | Default live pipeline demonstration; output is unitless |
| `sine-int8` | Same three live fields | Optional live model switch |
| `comfort-summer` | A's AHT temperature and humidity | Optional live model switch; result includes `comfort_score` from 0 to 99 |
| `autoencoder` | 16 model-ready values | Replay demonstration only in this guide |
| `people-counter` | 31 preprocessed doorway-window values | Replay demonstration only; no live feature-window producer is implemented yet |

The live `ml_setup` interface accepts at most 16 input bindings, so sending `people-counter` as a live setup returns `unsupported_live_model`. Its replay proves the model can run; a vector of zeros cannot prove entry, exit, or occupancy. The uploaded-model option also exists, but needs a separately uploaded model and its verified input shape. With the new B and C firmware, a live `comfort-summer` result also reaches C through `d6/board-b/comfort`. C's other cards use a separate snapshot path: B has a mock publisher, and the [temporary laptop relay](docs/d6/LIVE_LCD_TWO_SONARS.md) can publish real AHT, light, and separate Sonar A/B distances. Occupancy in that relay is a placeholder.

## Windows PowerShell: start Board B live ML

Use **normal PowerShell** for publishers and subscribers. Start the broker first using the Administrator PowerShell instructions in [usecase_commands.md](usecase_commands.md), then confirm fresh A and B heartbeats. The commands below use the laptop's local broker listener; the boards themselves still require the laptop Wi-Fi IP in their compiled credentials. Run from this checkout, not from a different branch.

**Window 1: watch status and results.** Leave this window open. A successful setup prints `"ok":true` with `"reason":"applied"`; subsequent results should say `"model":"sine-float"`, `"input_source":"remote:s3-001"` and `"status":"OK"` when A supplies fresh valid readings.

```powershell
Set-Location 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23'
$sub = "$env:ProgramFiles\mosquitto\mosquitto_sub.exe"
& $sub -h 127.0.0.1 -t 'pico/s3-002/ml/status' -t 'pico/s3-002/ml/result' -v
```

**Window 2: configure A and B, then select the default model.** `CLEAR` replaces each board's volatile sensor configuration. Board B's four aliases use typed `remote_profile` decoders; this is required for ML to read temperature, humidity and light. Keep the subscriber open in Window 1. All commands below are **normal PowerShell**.

```powershell
Set-Location 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23'
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"

& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-001/cmd' -m 'CLEAR'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-001/config' -f '.\config\board_a.cfg'
Start-Sleep -Seconds 2
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-001/cmd' -m 'RUN'

& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'STOP'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'CLEAR'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/config' -f '.\config\board_b_ml.cfg'
Start-Sleep -Seconds 2
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'RUN'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_sine_float.json'
```

## Switch live models on Board B

Run any one command in **Window 2** while sampling continues. Each accepted switch returns a new `generation` on `pico/s3-002/ml/status`. Watch `pico/s3-002/ml/result` for the selected model. A rejected setup leaves the previous live setup in place.

```powershell
# Switch to the INT8 sine demonstration.
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_sine_int8.json'

# Switch to the live AHT comfort score.
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_comfort_summer.json'

# Restore the default sine-float model.
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_sine_float.json'
```

These switches do not interrupt A's sampling. Only a successful, live `comfort-summer` result is forwarded to C's **LIVE COMFORT** card. The sine models remain visible on B's ML result topic; they are not converted to comfort scores.

## Show dynamic comfort on Board C

Boards B and C must be flashed with the firmware containing the `d6/board-b/comfort` publisher and subscriber. Leave A running its AHT feed and B running the typed sensor bridge above. In **normal PowerShell Window 2**, run:

```powershell
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-003/cmd' -m 'D6VIEW'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_comfort_summer.json'
```

Check B's `pico/s3-002/ml/result` for `"model":"comfort-summer"`, `"input_source":"remote:s3-001"`, `"status":"OK"`, and `comfort_score`. The same integer appears on `d6/board-b/comfort` and on C's landscape **LIVE COMFORT** card. Stop A's sensor feed to check expiry: B stops producing fresh scores after its inputs become stale; C shows **STALE** six seconds after its last accepted score. `RUN` on A should restore a fresh score. This is a live comfort check, while occupancy and the other LCD fields still require the B snapshot integration.

## Replay the other two bundled models

These are **model execution checks**, not live doorway tests. Run in **Window 2** while the broker and B are online. `STOP` pauses B's scheduler, so restore its live setup afterward. `ML OUTPUT publish` sends the replay result to the subscriber in Window 1. The console may also show the result.

```powershell
# Autoencoder: exactly 16 model-ready input values.
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'STOP'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'ML MODEL autoencoder'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'ML OUTPUT publish'
$autoValues = (1..16 | ForEach-Object { '0' }) -join ' '
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m "ML REPLAY $autoValues"

# People-counter: exactly 31 preprocessed doorway-window values.
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'ML MODEL people-counter'
$doorwayValues = (1..31 | ForEach-Object { '0' }) -join ' '
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m "ML REPLAY $doorwayValues"

# Resume B's scheduler and restore the default live model.
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -m 'RUN'
& $pub -h 127.0.0.1 -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_sine_float.json'
```

If `ML MODEL people-counter` reports `local export required`, that particular build lacks its bundled model data; use `ML LIST` in B's serial console to inspect availability before retrying. A real people-counting test needs Board A's timed crossing and a 31-value feature window, then Board B's validated occupancy update and snapshot publication. These are separate implementation steps.

To end the session, send `STOP` to A and B, press **Ctrl+C** in the subscriber and broker windows, then unplug boards if finished. See [usecase_commands.md](usecase_commands.md) for the full shutdown sequence.
