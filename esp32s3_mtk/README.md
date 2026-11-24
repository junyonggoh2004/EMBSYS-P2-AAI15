# Configurable ESP32-S3 firmware — ESP-IDF / µT-Kernel SMP

**Current D6 Board A (21 November 2026):** ESP32-S3 N16R8 with AHT, TSL2561 and two sonars, without radar. Use the [current pinout and diagram](../docs/d6/BOARD_A_SONAR_ONLY_WIRING.md), [`board_a.cfg`](../config/board_a.cfg), and [PowerShell sensor-test commands](../docs/d6/BOARD_A_SENSOR_TEST.md). GPIO1/2 are I2C; GPIO10/11 are Sonar A; GPIO6/7 are Sonar B. The generic three-sensor preset and older radar diagrams below document different fixtures.

With the broker and Board A running, `python tools/show_live_values.py --board A` from the repository root shows live temperature, humidity, light and valid sonar distances in PowerShell. `--board B` shows the same readings after MQTT relay to B. The viewer decodes the existing raw MQTT frames and needs no firmware change; see the [use case commands](../usecase_commands.md). Board B's default `sine-float` setup and optional switches for all five bundled models are in [ML_commands.md](../ML_commands.md). Its remote 31-reading `people-counter` window is experimental; a separate replay file tests model execution without claiming a validated crossing.

**Current D6 Board C (23 November 2026):** the isolated `LCDTEST` is still
available. `D6VIEW` draws accepted Board B MQTT snapshots in the physically
verified 284 x 76 landscape orientation. The user confirmed all mock fields
and an occupancy update from 2 to 3. The current v3 snapshot layout has
separate Sonar A and B distance lines; a missing echo displays `--`. Its
firmware builds and host tests pass, but it needs a fresh visual check after
flashing. Reissue `D6VIEW` after a reboot or an
isolated LCD pattern test. See [MQTT commands](../MQTT_commands.md) and the
[live LCD guide](../docs/d6/LIVE_LCD_TWO_SONARS.md). Real AHT, light and sonar
values use a temporary laptop snapshot relay; B's real comfort score reaches
C directly. B-owned occupancy and native full-snapshot publication remain open.

For the earlier proposed D6 three-board N16R8 fixture, see the
[board identification](../docs/d6/N16R8_BOARD_SETUP.md),
[pin-to-hole guide](../docs/d6/PIN_TO_HOLE_WIRING.md) and the
`BOARD=n16r8` preset. Earlier Super Mini evidence below remains historical.

This target uses main's complete configurable framework at upstream commit `49677c62c8738493f4e66667d1fe79afc947600d`, with optional AHT and TSL2561 I²C profiles and the existing asynchronous GPIO pulse driver. See the [breadboard guide](../SENSOR_BREADBOARD_README.md) for the full wiring tables, Windows build/flash/monitor instructions, preset commands and JSON formats. [WIRING.md](WIRING.md) contains the same complete connection tables.

The corrected firmware now has board evidence of all three sensors returning OK/error 0 together; see [VALIDATION.md](VALIDATION.md). The older shared-breadboard LCD observation was dim/white with no text. D6 now imports an opt-in LCD diagnostic from `lcd-output` at `b1c1675`; its separate fixture has historical visible-pattern evidence. Follow the [LCD-only map](WIRING.md#separate-lcd-only-breadboard-test-current-lcd_output-firmware) and [component guide](components/lcd_output/README.md), not the older shared LCD pins. Combined D6 panel testing is pending; startup leaves the display inactive. The three-sensor preset never configures LCD GPIO6–10. Upstream SPI flash diagnostics use another fixture and must not be run with the LCD attached.

Build with `tools/build.ps1 build`, flash with `tools/build.ps1 flash -Port COMx`, load the volatile three-sensor configuration with `tools/build.ps1 sensors -Port COMx`, then open `tools/build.ps1 monitor -Port COMx`. The helper loads `../config/three_sensors.cfg` via the existing command interface and STOP/CLEAR replaces volatile sensor/rule configuration. Without a preset, configure and RUN manually. Ctrl+C leaves the interactive monitor.

The default output folder is `build/`, separate from the previous sensor-only `build_mtk/`. A `-BuildDirectory` override permits a shorter output path on Windows. CMake uses response files and limits the SDK to the application's dependency closure; all shared components and the full runtime remain compiled. `tools/audit_configurable.py` audits the actual ELF and compiler inputs after every successful build.

The local `tools/local.env.ps1` points to the already installed, pinned ESP-IDF, toolchain and kernel; it is ignored by Git. Other machines must initialise the kernel submodule or configure an external kernel path, install the qualified ESP-IDF revision/Python requirements/tools, and create their own local settings. Exact SDK/kernel commits are in `dependencies.lock.json`; managed MQTT/TFLite dependencies remain pinned by main's `dependencies.lock`. The kernel source is the same revision used by main, and it is not modified by this integration.

Main's memory layout requires 2 MB quad PSRAM and 4 MB flash, including the fixed model partition. µT-Kernel owns the seven framework tasks, two idle tasks, message buffers, mutexes, alarms and supervision; the SDK compatibility shim does not run the FreeRTOS scheduler. SchedulerTask and its GPIO ISR are on physical CPU1, MQTTTask on CPU0. A captured TASKS/boot session is still required on this board.

See [MAIN_COMPONENT_REVIEW.md](../MAIN_COMPONENT_REVIEW.md) for the analysis completed before updating the workspace, [MTK_REDESIGN.md](../MTK_REDESIGN.md) for main's runtime/memory design, and [VALIDATION.md](VALIDATION.md) for actual checks and remaining board validation.
