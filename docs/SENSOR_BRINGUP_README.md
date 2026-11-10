# dev-jy-sensors — ESP32-S3 sensor firmware

The current ESP32-S3 target on **`dev-jy-sensors` uses ESP-IDF with µT-Kernel 3 SMP (uT-Kernel)**. Open **[AAI15-MTK.code-workspace](../AAI15-MTK.code-workspace)** and build **`esp32s3_mtk/`**.

## Current status

| Component / check | Status |
|---|---|
| AHT temperature and humidity sensor | Working in the recorded hardware tests |
| HC-SR04+ ultrasonic distance sensor | Working in the recorded hardware tests |
| Grove TSL2561 digital light sensor | Working in the recorded hardware tests |
| ST7789 TFT LCD | **Not working for now**; dim/white screen, no text. Connections can remain; LCD support is disabled in the current firmware. |
| ESP-IDF + µT-Kernel build | Clean compile/link and linked-kernel audit passed |

The working-sensor entries record the hardware test results. The migration validation records compilation and link checks; it does not contain a captured on-board run of the new µT-Kernel firmware. See [validation and board-test steps](../esp32s3_mtk/VALIDATION.md).

## Firmware and repository layout

| Location | Role |
|---|---|
| `esp32s3_mtk/` | Current ESP-IDF / µT-Kernel SMP sensor target |
| `AAI15-MTK.code-workspace` | VS Code workspace selecting the current target |
| `esp32s3/` | Earlier ESP32 firmware, retained separately |
| Root Pico sources and `CMakeLists.txt` | Earlier Pico project, retained separately |
| [docs/README-PICO-LEGACY.md](README-PICO-LEGACY.md) | Preserved original Pico README |

ESP-IDF supplies the build system, startup support and hardware definitions. **µT-Kernel runs task scheduling and application synchronisation.** The port's `freertos_shim` translates SDK compatibility calls into µT-Kernel operations; the original FreeRTOS scheduler is excluded from this target. This project is built from C sources, rather than an Arduino `.ino` sketch.

The acquisition task runs on kernel processor 1 (physical CPU0); the logger runs on processor 2 (physical CPU1). A µT-Kernel priority-inheritance mutex protects the shared sensor sample. Each sensor is read independently, and missing or failed measurements produce JSON `null` plus a status.

This target currently covers the three sensors and USB JSON output. LCD operation, networking, inference and the original Pico rule engine are outside its enabled functionality.

## Buses and pin summary

| Component | Interface | ESP32 signal pins | Supply |
|---|---|---|---|
| AHT | I²C | SDA GPIO1, SCL GPIO2 | 3V3 |
| Grove TSL2561 | I²C, shared with AHT | SDA GPIO1, SCL GPIO2 | 3V3 |
| HC-SR04+ | Digital GPIO pulse timing | TRIG GPIO4, ECHO GPIO5 | 3V3, compatible variant |
| ST7789 LCD | SPI, currently disabled | SCK GPIO6, MOSI GPIO7, RST GPIO9, DC GPIO8, CS GPIO10 | 3V3 |

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

The TSL2561 shares the I²C bus with the AHT. The driver checks **0x29, 0x39 and 0x49** and validates the light sensor's part ID. These addresses do not conflict with the AHT's 0x38.

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

## Build, flash and view readings

The local SDK, toolchain and kernel locations are already configured in ignored `esp32s3_mtk/tools/local.env.ps1` on the development machine. Other machines need their own setup; see the [target README](../esp32s3_mtk/README.md). Exact kernel/SDK revisions are recorded in [dependencies.lock.json](../esp32s3_mtk/dependencies.lock.json), and managed SDK dependencies in [dependencies.lock](../esp32s3_mtk/dependencies.lock).

1. Connect the ESP32-S3 with a USB data cable and close Arduino Serial Monitor or any other program using its COM port.
2. Open `AAI15-MTK.code-workspace` in VS Code.
3. Press **Ctrl+Shift+B → MTK: build**. Use this task so the current µT-Kernel target is selected.
4. In a VS Code PowerShell terminal, enter the target folder and flash. Replace `COM5` with the board's port shown in Device Manager.

```powershell
cd "<your clone of the repository>\esp32s3_mtk"

# Build and check the linked kernel (also available through Ctrl+Shift+B).
powershell.exe -NoProfile -File .\tools\build.ps1 build

# Upload all required images, then open the serial monitor.
powershell.exe -NoProfile -File .\tools\build.ps1 flash -Port COM5
powershell.exe -NoProfile -File .\tools\build.ps1 monitor -Port COM5
```

Building produces firmware on the computer. Flashing uploads it to the ESP32, which then executes it. The monitor displays JSON readings every few seconds. A missing sensor's measurement is `null`; zero lux is a valid reading. `display_connected:null` and `display_enabled:false` are expected while LCD support is disabled. Runtime processor fields should show `sensor_processor:1` and `logger_processor:2`.

Output files are under `esp32s3_mtk/build_mtk/`, including `aai15_esp32s3_mtk_sensors.bin`. The build task does not flash automatically.

## Kernel source and validation

The target provisionally uses [sirfonzie/mtk3smp-esp32s3](https://github.com/sirfonzie/mtk3smp-esp32s3), pinned to the revision in the dependency lock. The requirement document specifies the course µT-Kernel 3 SMP port without identifying a source URL; lecturer confirmation of this public port as the course-approved base is still pending.

See [esp32s3_mtk/README.md](../esp32s3_mtk/README.md) for task/core details and [esp32s3_mtk/VALIDATION.md](../esp32s3_mtk/VALIDATION.md) for completed checks and remaining on-board verification. The complete wiring tables are also kept in [esp32s3_mtk/WIRING.md](../esp32s3_mtk/WIRING.md).
