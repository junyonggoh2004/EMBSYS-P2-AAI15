# Run the first embedded ML version

The first ML implementation came from `dev-jy-ml` and was integrated into `main` through `d4-live-ml-sitt`. It uses the existing ESP-IDF / µT-Kernel firmware and unchanged [breadboard wiring](../../esp32s3_mtk/WIRING.md). The LCD remains disabled and non-functional.

## Start in VS Code

1. Open `AAI15-MTK.code-workspace`. Close Arduino Serial Monitor and any other serial monitor.
2. Connect the board and check its COM number. The examples use `COM5`.
3. Press **Ctrl+Shift+B**, select **MTK: build**, and wait for success.
4. Choose **Terminal → Run Task → MTK: flash**, select `COM5`, and wait for completion.
5. Choose **Terminal → Run Task → MTK: load sensors and ML**, select `COM5`. Wait for `ML: OK PRESET sine-float`, `ML: OK RUN` and the final `RUN` acknowledgement.
6. Choose **Terminal → Run Task → MTK: monitor**, select `COM5`. Do not press RESET. Look for timestamped sensor and ML rows with readings, units and status. The formatted monitor skips sensor rows for missing readings and timeouts. Use **MTK: monitor raw** for those diagnostics, full JSON and hex frames.

Equivalent PowerShell commands, from the workspace's `esp32s3_mtk` folder, **one at a time**:

```powershell
powershell.exe -NoProfile -File .\tools\build.ps1 build
powershell.exe -NoProfile -File .\tools\build.ps1 flash -Port COM5
powershell.exe -NoProfile -File .\tools\build.ps1 ml -Port COM5
powershell.exe -NoProfile -File .\tools\build.ps1 monitor -Port COM5
```

The ML loader uses STOP/CLEAR and replaces volatile sensors/rules with the three-sensor preset. A reset loses this configuration; run the `ml` action again. Close the monitor with **Ctrl+C** before running a loader. A loader acknowledgement confirms configuration, not a successful inference. Look for `ML OK` with numeric outputs. In the raw monitor, the equivalent JSON has `status: OK`.

There are no additional Arduino libraries to install. Desktop model-validation packages are separate from ESP-IDF; they are not required to flash or use the board.

## If distance is timing out: test AHT and light without typing commands

The captured first board run shows valid AHT/light readings and distance timeouts. ML correctly reports MISSING_OR_STALE when the default four-feature mapping lacks distance; successful inference is not established by that capture.

Close the monitor with **Ctrl+C**. Flash the rebuilt firmware (the update fixes Backspace handling, trims command whitespace, and removes repetitive scheduler debug lines). Then run **Terminal → Run Task → MTK: load AHT and light ML**, choose COM5, wait for acknowledgements, and open **MTK: monitor**. No manual serial commands or rewiring are needed. This preset configures only AHT and LIGHT and binds temperature, humidity and lux to the sine model.

Equivalent loader command after closing the monitor:

```powershell
powershell.exe -NoProfile -File .\tools\build.ps1 ml -Port COM5 -Inputs aht-light
```

This replaces volatile sensors/rules. Distance wiring can remain connected. Choose the original **MTK: load sensors and ML** task when distance acquisition is working again.

## What the default model receives

The firmware uses the same numeric decoder for sensor JSON and ML. It never parses printed JSON back into input values.

| Feature | Source field | Configured minimum | Maximum |
|---|---|---:|---:|
| 0 | AHT `temperature_c` | -10 °C | 50 °C |
| 1 | AHT `humidity_pct` | 0% RH | 100% RH |
| 2 | LIGHT `light_lux` | 0 lux | 1000 lux |
| 3 | DISTANCE `distance_cm` | 2 cm | 400 cm |

Each value becomes `u = clamp((value - min)/(max - min), 0, 1)`. For the scalar sine models, the actual input is `2π × mean(all bound u values)`. The genuine pretrained network returns one numeric output. It approximates a sine function; **it does not predict occupancy, temperature or anomalies**.

ML output includes model ID, configuration generation, physical `raw_features`, transformed `inputs`, model `outputs`, elapsed inference request time and status. `raw_features` follows the binding indices printed by `ML SHOW`. The elapsed `invoke_us` field includes the µT-Kernel request/reply overhead; it is not an isolated kernel benchmark.

Missing or failed sensor readings invalidate the affected feature. Missing/stale inputs emit `outputs:null` with `MISSING_OR_STALE`; inconsistent timestamps emit `SAMPLE_SKEW`. Zero lux is valid. Default freshness limit is 5000 ms, maximum timestamp skew 2500 ms, output period 2000 ms. An initial null line before all sensors have produced a sample is normal.

## Switch models without rebuilding

Use the monitor to send these commands **one line at a time**:

```text
STOP
ML PRESET sine-int8
ML RUN
RUN
```

For the different-shape model:

```text
STOP
ML PRESET autoencoder
ML RUN
RUN
```

| ID | Origin | Actual tensor contract | Preset mapping |
|---|---|---|---|
| `sine-float` | Official pretrained TFLM Hello World | Float32 `[1,1]` → `[1,1]`; 3164 bytes | Mean of four normalized fields, scaled to [0,2π] |
| `sine-int8` | Official pretrained TFLM Hello World | Int8 `[1,1]` → `[1,1]`; 2704 bytes | Same, with actual tensor quantization |
| `autoencoder` | Existing repository model | Int8 `[1,16]` → `[1,16]`; 16440 bytes | Four normalized fields repeated four times |

The autoencoder preset is explicitly a shape/execution demonstration. Its repeated features do not reproduce its training distribution. No meaningful anomaly decision is made. All outputs are exposed as a numeric vector, with no forced MSE scoring or probability interpretation.

`ML MODEL <id>` changes the model while retaining bindings, but disables ML until `ML RUN` validates them. `ML PRESET <id>` supplies the appropriate demonstration bindings. Models are immutable bundled assets selected at runtime; installing arbitrary new `.tflite` files over serial is not implemented in this first version. The existing MQTT transfer subsystem is retained, but model uploads or other model-loading flows change the model revision and cause this ML pipeline to stop with `MODEL_CHANGED` until reconfigured.

## Run the sensor configuration demos

Close the serial monitor. For a physical wiring change, disconnect USB before moving leads, then reconnect USB. In VS Code choose **Terminal → Run Task → MTK: load demo configuration**, select the file and serial port, then reopen **MTK: monitor**. The loader sends the sensor registrations, model selection and ML bindings from the selected file. The same firmware image handles all profiles. Apply the selected profile again after a reset.

| Profile | Configured sensor outputs | Features used by `sine-float` |
|---|---|---|
| [no-light.cfg](../../config/demos/no-light.cfg) | AHT and DISTANCE | Temperature, humidity, distance |
| [no-aht.cfg](../../config/demos/no-aht.cfg) | LIGHT and DISTANCE | Light, distance |
| [dual-ultrasonic.cfg](../../config/demos/dual-ultrasonic.cfg) | LIGHT, DISTANCE and DISTANCE2 | Light, first distance, second distance |

Each file sets sensor names, drivers, pins and timing. The three files use the same scalar model and set consecutive ML binding indices from zero. Each bound value is scaled, and the model input is `2π × mean(scaled values)`. A sensor that is outside the selected profile has no polling slot or ML binding. Use **MTK: load sensors and ML** to restore the original three-sensor setup.

For the two-ultrasonic profile, keep the original HC-SR04+ on GPIO4/5 and the TSL2561 on GPIO1/2. Remove the AHT. The table assumes the second module supports 3.3 V operation and a 3.3 V-compatible Echo signal. A module with a 5 V Echo needs level shifting before it connects to the ESP32. [3.3–5 V ultrasonic module datasheet](https://cdn.sparkfun.com/assets/a/8/9/c/a/HC-SR04-33_Datasheet.pdf)

| Second ultrasonic | Connection on the current breadboard |
|---|---|
| VCC | 3V3, E4 |
| GND | Common GND, E7 |
| TRIG | GPIO11 to A15, then E15 to TRIG |
| ECHO | GPIO12 to A16, then E16 to ECHO |

Use free rows and adjust `gpio.trig` and `gpio.echo` in the file if GPIO11/12 are occupied by another fixture. Keep the two TRIG/ECHO pairs separate. The first ultrasonic runs periodically. A fresh LIGHT measurement triggers DISTANCE2 after the light driver's integration wait of at least 120 ms, leaving time for the first 30 ms echo capture. This profile requires a working light sensor to trigger the second sonar. Point the modules at separate targets and confirm both values before the demo.

On 10 November, `no-light.cfg` and `no-aht.cfg` each returned numeric inference output on the board. `dual-ultrasonic.cfg` registered three sources and continued reporting light and the original ultrasonic while the unconnected DISTANCE2 reported a timeout. Verification with two physically connected modules is pending.

For a custom file, run the following from `esp32s3_mtk`.

```powershell
powershell.exe -NoProfile -File .\tools\build.ps1 ml -Port COM5 -ConfigFile ..\config\demos\no-light.cfg
```

## Change the sensor-to-model mapping

Only supported decoded fields can be selected. This version supports AHT over I²C, TSL2561 over I²C and GPIO pulse distance. Other UART/SPI drivers still need typed measurement adapters; their raw bytes cannot automatically be used as physical values.

To run a model using **only distance**, leaving the three sensors configured:

```text
STOP
ML MODEL sine-float
ML RESET
ML BIND 0 DISTANCE distance_cm 2 400
ML RUN
RUN
```

To replace that GPIO input with I²C light, with the same model and firmware:

```text
STOP
ML BIND 0 LIGHT light_lux 0 1000
ML RUN
RUN
```

Missing sensors not referenced by a binding do not block inference. Sources must already exist in the scheduler and expose the chosen field. Configure replacement sensors with existing BEGINCFG commands first, then set bindings. This is a supported bus/driver substitution, not automatic discovery of unknown hardware.

| Command | Behavior |
|---|---|
| `ML HELP` / `ML SHOW` | Commands, active model, generation, timing and all bindings |
| `ML PRESET [id]` | Load a bundled model and the corresponding current-sensor mappings; default sine-float |
| `ML MODEL <id>` | Load a bundled model, retain mappings, disable ML |
| `ML RESET` | Clear bindings, disable ML; selected model remains loaded |
| `ML BIND <index> <source> <field> <min> <max>` | Set a binding, validate its field/range and clear cached samples |
| `ML UNBIND <index>` | Remove a binding and disable ML |
| `ML TIMING <period_ms> <max_age_ms> <max_skew_ms>` | Period and age 100–60000 ms; skew 0–max age |
| `ML RUN` / `ML STOP` | Enable validated ML / stop ML independently of sensor sampling |
| `STOP` / `RUN` | Existing sensor sampling controls; STOP clears cached ML samples |

Configuration changes and `ML RUN` require sensor sampling stopped. Use contiguous binding indices beginning at zero. Scalar models reduce any 1–16 bindings to one input; the vector model requires exactly 16. Missing mappings are rejected rather than silently padded. Configuration and model changes clear old samples; inference waits for fresh data. Invalid model loads disable the pipeline and attempt to reload the previous bundled model. Do not combine legacy `infer.set`/anomaly rules with this demo; both use the same interpreter and manager.

## MQTT control checkpoint (D4 Part 2)

Since the D4 checkpoint, the existing `pico/<node>/cmd` subscription also accepts the same `ML` text commands as the serial console. MQTTTask queues each command for SchedulerTask; the ML parser and model state are not called from the network callback. For example, with the board connected to a broker, send the following payloads **in order** to the board's own command topic (replace `<node>` with its configured node ID):

```text
STOP
ML PRESET sine-int8
ML RUN
RUN
```

Watch the board's serial console for `ML: OK PRESET sine-int8`, `ML: OK RUN`, and subsequent ML JSON with numeric outputs. `ML SHOW` can be sent on the same topic without clearing cached samples. Other command payloads continue through the existing MQTT command handler. MQTT delivery was verified on the board on 10 Nov 2026: `ML SHOW` sent on the command topic, and sent by another board with `cmd.to` (D1 task 1.4, [parity/logs/mtk/d1-mqtt/](../../parity/logs/mtk/d1-mqtt/)).

This is **not yet live switching**: `STOP` pauses sensor sampling, and the commands are separate MQTT messages. The one-command format for model, local/remote input and local/published output still needs team agreement. Remote ML input and MQTT publishing of ML results are not implemented here. Tasks 1.4 and 2.1, which this needed first, are done; the rest is D4 part 2 in the [README task lists](../../README.md#5-d4-task-checklist).

## Validation and remaining work

The [10 November board capture](BOARD_VALIDATION.md) verifies three sensors, swaps between all three bundled models, and two completed S3 vector-kernel calls during `sine-int8` inference on `dev-jy-ml`. Use the same checks on the combined MQTT firmware before recording an integration-board result. [Host and build validation](VALIDATION.md)

Keep the board running at least ten minutes, observe changing outputs as sensor inputs change, then test the model and binding swaps above. Check missing-data status by disabling a configured source or unplugging a sensor with USB disconnected. Capture TASKS before/after and verify stack headroom, watchdog stability and sensor/rule operation. Compare recorded actual model inputs with the desktop reference tool before claiming target numerical agreement.

Later work from the [deliverables plan](README.md): arbitrary model-package installation and manifest persistence, more driver adapters, model rollback fault tests on hardware, and MQTT ML-result publishing. Existing raw sensor MQTT behavior is preserved; ML result JSON is currently local console output.
