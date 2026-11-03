# dev-jy-ml — configurable sensors and embedded ML

Embedded ML branch: see the [dev-jy-ml deliverables plan](docs/embedded-ml/README.md) for configurable sensor inputs, replaceable pretrained models and the later MQTT milestone. A first implementation is available on `dev-jy-ml`: see the [ML run guide](docs/embedded-ml/RUNNING.md). Board ML verification is pending.

This workspace now uses the configurable components from **main commit `49677c62c8738493f4e66667d1fe79afc947600d`**, adapted for the current breadboard. Firmware runs on **ESP-IDF with µT-Kernel 3 SMP**. The analysis completed before updating the workspace is in [MAIN_COMPONENT_REVIEW.md](MAIN_COMPONENT_REVIEW.md).

## Embedded ML in this branch

The current implementation feeds decoded AHT temperature/humidity, TSL2561 light and HC-SR04+ distance into configurable model bindings. Run **MTK: load sensors and ML** for the full setup, including ultrasonic distance. The LCD remains disabled; existing wiring is unchanged. MQTT ML-result publishing is deferred.

The default pretrained **sine-float** model is a 3,164-byte Float32 dense network (1 → 16 → 16 → 1), trained on synthetic x → sin(x) pairs. The four sensor measurements are scaled to [0,1], averaged and multiplied by 2π; the network predicts the sine of that scalar. It does not predict occupancy or environmental conditions. Runtime alternatives are the 2,704-byte sine-int8 model and the existing 16-input autoencoder demonstration.

See the [model specifications and deliverables](docs/embedded-ml/README.md), [step-by-step user guide](docs/embedded-ml/RUNNING.md) and [validation results](docs/embedded-ml/VALIDATION.md). Firmware builds, host regressions and desktop model inference pass; successful ESP32 ML inference is still awaiting a captured result.

## Current sensor status

| Item | Status |
|---|---|
| AHT temperature/humidity, HC-SR04+ distance, Grove TSL2561 light | Confirmed reading together on the corrected ESP-IDF / µT-Kernel firmware in the user-provided 3 November runtime capture |
| ST7789 LCD | **Not working for now**: dim/white screen, no text; keep existing wiring, display remains disabled |
| Active firmware | Main's shared EdgeCore + complete µT-Kernel runtime, with optional I²C sensor profiles |
| Current integrated firmware validation | See [VALIDATION.md](esp32s3_mtk/VALIDATION.md); historical sensor tests are separate from new firmware execution |

**Use the wiring tables on this page for this fixture.** The upstream test guides use alternative pin layouts for one-sensor, SPI-flash, radar and loopback fixtures. Their commands are not this breadboard's configuration. In particular, GPIO1/2 are occupied by the two I²C sensors here, and GPIO10 is wired to LCD CS; do not run the upstream SPI/flash tests with the LCD attached.

## Open and run

1. Open [AAI15-MTK.code-workspace](AAI15-MTK.code-workspace) in VS Code. It selects only `esp32s3_mtk/`.
2. Connect the ESP32 by USB data cable and close Arduino Serial Monitor. Find its port in Device Manager → Ports (COM & LPT).
3. Press **Ctrl+Shift+B → MTK: build**.
4. In Terminal → New Terminal, run these commands **one at a time**. Replace `COM5` with the actual port.

```powershell
cd "C:\Users\junyo\OneDrive\Desktop\ES-P2-AAI15\ES-P2-AAI15\esp32s3_mtk"
powershell.exe -NoProfile -File .\tools\build.ps1 build
powershell.exe -NoProfile -File .\tools\build.ps1 flash -Port COM5
powershell.exe -NoProfile -File .\tools\build.ps1 sensors -Port COM5
powershell.exe -NoProfile -File .\tools\build.ps1 monitor -Port COM5
```

The **sensors** command loads [config/three_sensors.cfg](config/three_sensors.cfg) through the normal REPL: it sends STOP, CLEAR, three configurations, SHOW and RUN. CLEAR replaces volatile sensors/rules. It checks acknowledgements and stops if a configuration is refused. Close the monitor before running it. The monitor accepts typed commands; **Ctrl+C** closes it.

The preset is volatile: load it again after resetting or reflashing. Nothing samples automatically on a fresh boot until configured and RUN is sent. You can instead paste the preset's non-comment lines into the serial monitor, one line at a time.

The full framework uses main's **4 MB flash / 2 MB quad PSRAM** layout. Check PSRAM startup and TASKS on the actual board; the small previous sensor-only build did not require PSRAM. Build output is now `esp32s3_mtk/build/esp32s3_mtk.bin`, with the ELF, bootloader and partition table beside it. The build task does not upload automatically.

## Configurable components and readings

| Sensor | Configuration | Pins / address | Valid raw frame |
|---|---|---|---|
| AHT | `proto=i2c`, `i2c.sensor=aht` | SDA1/SCL2, 0x38 | Native six bytes: status + two packed 20-bit values |
| TSL2561 | `proto=i2c`, `i2c.sensor=tsl2561` | SDA1/SCL2, preset 0x29 | ch0 LE16, ch1 LE16, rounded lux LE32 |
| HC-SR04+ | Existing `proto=gpio`, `gpio.mode=pulse` | TRIG4/ECHO5 | Existing pulse width LE16 + valid byte |
| Generic I²C devices | Default `i2c.sensor=raw` | Configured through BEGINCFG | Existing raw register-read behaviour |

Profiles implement full sensor commands, bounded busy checks, conversion waits, cancellation and retries. They use the same hardware HAL and SchedulerTask as main. AHT conversion and TSL integration waits return pending work; they do not sleep inside the bus driver. The preset reads every 2 s and gives the I²C acquisitions a 600 ms deadline, distance 100 ms with a 30 ms echo capture limit.

The console prints supplementary JSON in physical units alongside the framework's raw lines:

```json
{"sensor":"AHT","timestamp_ms":1234,"temperature_c":25.00,"humidity_pct":50.00,"status":"OK","error":0}
{"sensor":"LIGHT","timestamp_ms":1240,"light_lux":0,"status":"OK","error":0}
{"sensor":"DISTANCE","timestamp_ms":1250,"distance_cm":null,"echo_us":null,"status":"TIMEOUT","error":-1}
```

These are illustrative formats, not captured board measurements. A failed or missing configured sensor produces JSON `null` and a status; zero lux remains valid. Failed acquisitions do not publish successful raw frames to the rules/MQTT pipeline. Physical-unit JSON is console output; the existing MQTT raw/line topics are preserved. This framework does not emit the old single combined sensor-only JSON object.

`SHOW` lists the three sensors and their profiles; `TASKS` reports the real runtime tasks, stack headroom, heap and core residency. Main pins **SchedulerTask to physical CPU1**, MQTTTask to CPU0. This supersedes the old bring-up task/core arrangement. HELP, RUN, STOP, CLEAR, rules, timing modes, application events, networking and inference remain available through main's components. Without credentials, the network task runs offline. No LCD driver or automatic SPI test is started.

## Repository guide

| Location | Purpose |
|---|---|
| `esp32s3_mtk/` | Active configurable ESP-IDF/µT-Kernel target |
| `libraries/EdgeCore/src/` | Main's shared scheduler, buses, rules, events and inference interfaces, plus optional sensor profiles |
| `config/three_sensors.cfg` | Current hardware preset; pins can be changed without rebuilding |
| [MAIN_COMPONENT_REVIEW.md](MAIN_COMPONENT_REVIEW.md) | Component-by-component review, decisions and scope |
| [docs/MAIN_BRANCH_README.md](docs/MAIN_BRANCH_README.md) | Preserved main guide and upstream feature/test documentation |
| [docs/SENSOR_BRINGUP_README.md](docs/SENSOR_BRINGUP_README.md) | Previous branch guide, kept as historical documentation |
| `legacy/esp32s3_sensor_bringup/` | Previous small sensor-only firmware; excluded from the active workspace/build |
| `legacy/esp32s3_idf_initial/` | Earlier untracked ESP32 experiment, preserved separately |
| `tools/`, `tests/`, `parity/` | Upstream tooling, host tests and historical target evidence, plus sensor tests |

Historical guides may describe different fixtures, output formats or build folders. Use this guide and [WIRING.md](esp32s3_mtk/WIRING.md) for the active configuration.

## Complete breadboard connections

These tables retain the Version 2 layout. They apply to the ESP32-S3 Super Mini, AHT temperature/humidity module, **3.3 V-compatible HC-SR04+**, Grove TSL2561 and the existing ST7789 display wiring. Disconnect USB before changing connections. Follow the printed pin labels on each module.

### Breadboard layout and jumper leads

- On the terminal strip, **A–E share the same numbered row**. F–J share a separate row on the other side of the centre gap. Adjacent numbered rows are separate.
- The ESP32 and modules remain outside the breadboard. Each connection can use **device male header → female-to-female lead → male-to-male lead → breadboard hole**. Seat the male-to-male pin fully inside the female connector.
- The light sensor uses its Grove cable; route its four signal leads to the holes listed below, using adapters as required by the cable ends. Identify leads by signal, rather than assuming a colour or connector orientation.
- **N1, N2 and N3 are chosen hole names, not printed breadboard coordinates.** Use your selected top hole and the next two consecutive holes in the **same continuous blue/minus power-rail section**. A split in a rail breaks continuity.
- All grounds are common. Power comes from the ESP32 **3V3** pin; the 5V pin is not used in this layout.

### ESP32-S3 → breadboard

| ESP32 printed pin | Breadboard hole | Purpose | Current firmware |
|---|---|---|---|
| 3V3 | A4 | Shared 3.3 V supply | Used |
| GPIO1 | A5 | Shared I²C SDA for AHT and TSL2561 | Used |
| GPIO2 | A6 | Shared I²C SCL for AHT and TSL2561 | Used |
| GND | A7 | Shared ground | Used |
| GPIO4 | A8 | Ultrasonic TRIG output | Used |
| GPIO5 | A9 | Ultrasonic ECHO input | Used |
| GPIO6 | A10 | LCD SPI clock | Retain; LCD disabled |
| GPIO7 | A11 | LCD SPI MOSI | Retain; LCD disabled |
| GPIO9 | A12 | LCD reset | Retain; LCD disabled |
| GPIO8 | A13 | LCD data/command | Retain; LCD disabled |
| GPIO10 | A14 | LCD chip select | Retain; LCD disabled |

### AHT temperature/humidity → breadboard

| AHT printed pin | Breadboard hole | Connected through that row to |
|---|---|---|
| VCC / VIN | E4 | ESP32 3V3 at A4 |
| SDA | E5 | ESP32 GPIO1 at A5 |
| SCL | E6 | ESP32 GPIO2 at A6 |
| GND | E7 | ESP32 GND at A7 |

The AHT uses I²C address **0x38**. Position wires by the module's printed labels; pin order can differ between AHT boards.

### HC-SR04+ ultrasonic → breadboard

| Ultrasonic printed pin | Breadboard hole | Connected through that row to |
|---|---|---|
| VCC | C4 | ESP32 3V3 at A4 |
| TRIG | E8 | ESP32 GPIO4 at A8 |
| ECHO | E9 | ESP32 GPIO5 at A9 |
| GND | C7 | ESP32 GND at A7 |

This layout uses the identified **HC-SR04+ with 3.3 V operation and a 3.3 V-compatible ECHO output**, so the ECHO voltage divider is omitted. It is not the wiring specification for a standard 5 V HC-SR04.

### Grove TSL2561 digital light sensor → breadboard

| Grove cable signal | Breadboard hole | Connected through that row to |
|---|---|---|
| VCC | D4 | ESP32 3V3 at A4 |
| SDA | D5 | ESP32 GPIO1 at A5; shared with AHT |
| SCL | D6 | ESP32 GPIO2 at A6; shared with AHT |
| GND | D7 | ESP32 GND at A7 |

The TSL2561 shares the I²C bus with the AHT. The profile validates the part ID at its configured address. The preset uses **0x29**; set `i2c.addr` to **0x39** or **0x49** if that is your module's address. These addresses do not conflict with the AHT's 0x38.

### ST7789 TFT LCD → breadboard (retain existing wiring)

**LCD is not working for now:** it has shown a dim/white screen without text. Its existing connections can remain while the other sensors are used. The current µT-Kernel firmware does not initialise or update the LCD and leaves GPIO6–10 unused. Keeping it connected does not enable display output.

| LCD printed pin | Breadboard hole | Electrical connection | Current firmware |
|---|---|---|---|
| GND | Blue rail N2 | Ground via N1 and B7 | Retain |
| VCC | B4 | ESP32 3V3 via A4 | Retain |
| SCL | E10 | ESP32 GPIO6 via A10; SPI SCK | Retain; unused |
| SDA | E11 | ESP32 GPIO7 via A11; SPI MOSI | Retain; unused |
| RST | E12 | ESP32 GPIO9 via A12 | Retain; unused |
| DC | E13 | ESP32 GPIO8 via A13 | Retain; unused |
| CS | E14 | ESP32 GPIO10 via A14 | Retain; unused |
| BL | Blue rail N3 | Ground; retained active-low BL arrangement | Retain; module-specific |

The LCD's **SCL/SDA labels mean SPI clock/MOSI**, not the I²C SCL/SDA used by the AHT and TSL2561. Do not join those display signals to rows 5/6. No MISO connection is used.

The BL-to-ground entry preserves the active-low backlight arrangement previously selected for this display module. BL wiring differs between ST7789 breakout boards; this is not a universal ST7789 pinout.

### Ground-rail jumper

| From | To | Lead | Purpose |
|---|---|---|---|
| B7 | Blue rail N1 | One male-to-male wire | Connect the blue rail to ESP32 GND through row 7 |
| LCD GND | Blue rail N2 | LCD connection above | Share the same grounded rail section |
| LCD BL | Blue rail N3 | LCD connection above | Retain the existing module's BL connection |

**B7 is ground. B5 is SDA and must not be connected to the ground rail.** N2/N3 are connected through the continuous rail; no extra N1→N2 or N2→N3 jumper is needed within that section.

### Resistors

| Connection | Extra resistor in this setup |
|---|---|
| HC-SR04+ ECHO → GPIO5 | None for the identified 3.3 V-compatible module |
| AHT/TSL2561 I²C SDA and SCL | No additional pull-ups with the existing breakout modules' pull-ups; pull-ups must go to 3V3 |
| LCD signal/BL connections | No new resistor added; preserve the existing breakout arrangement |

Bare sensors or different breakout revisions may need different power, pull-up or backlight circuitry. These tables describe the current modules and retained wiring.
