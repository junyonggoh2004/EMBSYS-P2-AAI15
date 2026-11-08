# Configurable ESP32-S3 firmware — ESP-IDF / µT-Kernel SMP

This target uses main's complete configurable framework at upstream commit `49677c62c8738493f4e66667d1fe79afc947600d`, with optional AHT and TSL2561 I²C profiles and the existing asynchronous GPIO pulse driver. See the [breadboard guide](../SENSOR_BREADBOARD_README.md) for the full wiring tables, Windows build/flash/monitor instructions, preset commands and JSON formats. [WIRING.md](WIRING.md) contains the same complete connection tables.

The corrected firmware now has board evidence of all three sensors returning OK/error 0 together; see [VALIDATION.md](VALIDATION.md). The LCD remains non-functional (dim/white, no text); all existing connections can remain. The three-sensor preset never configures LCD GPIO6–10. Upstream SPI flash diagnostics use another fixture and must not be run with the LCD attached.

Build with `tools/build.ps1 build`, flash with `tools/build.ps1 flash -Port COMx`, load the volatile three-sensor configuration with `tools/build.ps1 sensors -Port COMx`, then open `tools/build.ps1 monitor -Port COMx`. The helper loads `../config/three_sensors.cfg` via the existing command interface and STOP/CLEAR replaces volatile sensor/rule configuration. Without a preset, configure and RUN manually. Ctrl+C leaves the interactive monitor.

The default output folder is `build/`, separate from the previous sensor-only `build_mtk/`. A `-BuildDirectory` override permits a shorter output path on Windows. CMake uses response files and limits the SDK to the application's dependency closure; all shared components and the full runtime remain compiled. `tools/audit_configurable.py` audits the actual ELF and compiler inputs after every successful build.

The local `tools/local.env.ps1` points to the already installed, pinned ESP-IDF, toolchain and kernel; it is ignored by Git. Other machines must initialise the kernel submodule or configure an external kernel path, install the qualified ESP-IDF revision/Python requirements/tools, and create their own local settings. Exact SDK/kernel commits are in `dependencies.lock.json`; managed MQTT/TFLite dependencies remain pinned by main's `dependencies.lock`. The kernel source is the same revision used by main, and it is not modified by this integration.

Main's memory layout requires 2 MB quad PSRAM and 4 MB flash, including the fixed model partition. µT-Kernel owns the seven framework tasks, two idle tasks, message buffers, mutexes, alarms and supervision; the SDK compatibility shim does not run the FreeRTOS scheduler. SchedulerTask and its GPIO ISR are on physical CPU1, MQTTTask on CPU0. A captured TASKS/boot session is still required on this board.

See [MAIN_COMPONENT_REVIEW.md](../MAIN_COMPONENT_REVIEW.md) for the analysis completed before updating the workspace, [MTK_REDESIGN.md](../MTK_REDESIGN.md) for main's runtime/memory design, and [VALIDATION.md](VALIDATION.md) for actual checks and remaining board validation.
