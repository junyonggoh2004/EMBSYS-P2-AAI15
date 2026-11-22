# ESP32-S3: fresh micro T-Kernel SMP sensor target

This is a separate ESP-IDF project for the ESP32-S3 Super Mini, on the project branch `dev-jy-sensors`. It does not build or import the existing Pico project, the older `esp32s3/` folder, or the Arduino sketch.

Scope: AHT10/AHT20 temperature/humidity, 3.3 V-compatible HC-SR04+, and Grove TSL2561. The Version 2 LCD is deliberately disabled; GPIO6–10 are not configured by this application. Networking, the Pico rule engine and inference code have not been migrated into this target.

## Hardware status and connections

The team confirms that the AHT, HC-SR04+ and Grove TSL2561 sensors have worked in hardware testing. The ST7789 LCD is **not working for now** (dim/white screen without text). All existing Version 2 connections, including the LCD connections, can remain. The current firmware leaves LCD GPIO6–10 unused and does not initialise the display.

See [WIRING.md](WIRING.md) for every ESP32, sensor, LCD and ground-rail connection, and the [branch README](../README.md) for the complete branch guide. These hardware results are user-reported; the recorded migration checks establish a successful build and link, with on-board µT-Kernel execution still to be captured.

## Kernel provenance

The requirement document specifies the **course micro T-Kernel 3 SMP port**, but does not identify its source. This target provisionally uses [sirfonzie/mtk3smp-esp32s3](https://github.com/sirfonzie/mtk3smp-esp32s3) at the exact revision in `dependencies.lock.json`. Confirm with the lecturer that this is the course-approved port before claiming course compliance. A successful build is not that confirmation.

The port uses ESP-IDF for startup, the compiler and hardware definitions. It intercepts scheduler startup and runs micro T-Kernel; its `freertos_shim` translates SDK compatibility calls into micro T-Kernel operations. The previous FreeRTOS scheduler is excluded. SDK configuration names containing `FREERTOS` therefore still exist; their presence does not mean that scheduler runs.

## Open and build in VS Code

Open `ESP32S3-MTK.code-workspace` in this folder, or the `AAI15-MTK.code-workspace` entry at the repository root. Only this new target is included in that workspace. Do not use the repository root's Pico CMake target or the ESP-IDF extension's unconfigured default Build button.

Run **Terminal → Run Build Task → MTK: build** (Ctrl+Shift+B). The task calls `tools/build.ps1`, pins dependency revisions and runs ESP-IDF plus the linked-image audit. A full `build_mtk/` folder is generated here, independently of all other build folders.

The local machine's external SDK/kernel paths are recorded in ignored `tools/local.env.ps1`. Nothing changes the global Arduino setup or the global PATH. For another machine, create that file with `MtkKernelPath`, `MtkIdfPath`, `MtkToolsPath`, `MtkPythonPath`, `MtkCmakeBin`, and `MtkNinjaBin`, or use the environment variables documented by `tools/build.ps1`. Install the pinned ESP-IDF revision's Python requirements and its Xtensa 15.2.0/ULP tools. `dependencies.lock.json` pins the kernel and SDK; the ESP-IDF-generated `dependencies.lock` pins the port's managed SDK component dependency. Some networking SDK components compile because the port declares them, but this application does not start networking.

```powershell
powershell.exe -NoProfile -File .\tools\build.ps1 build
powershell.exe -NoProfile -File .\tools\build.ps1 audit
# Only when ready to flash; replace COMx with the actual port:
powershell.exe -NoProfile -File .\tools\build.ps1 flash -Port COMx
powershell.exe -NoProfile -File .\tools\build.ps1 monitor -Port COMx
```

Firmware output: `build_mtk/aai15_esp32s3_mtk_sensors.bin` and the matching ELF, bootloader and partition table. Use the ESP-IDF flash command so all images go to their required offsets. No board is flashed automatically by Build.

## Tasks, cores and synchronisation

| Task | Priority | Kernel processor / physical CPU | Purpose |
|---|---:|---|---|
| Acquisition | 5 | processor 1 / CPU0 | Sole I2C owner; reads all three sensors |
| Logger | 10 | processor 2 / CPU1 | Copies the latest complete sample, then formats and sends JSON |
| Idle CPU0 | lowest | processor 1 / CPU0 | Keeps a runnable task while acquisition sleeps |
| Idle CPU1 | lowest | processor 2 / CPU1 | Keeps a runnable task while logger sleeps |

Acquisition and logger both carry `TA_FPU` and fixed processor affinity, as required by this Xtensa port's per-task floating-point state handling. A priority-inheritance micro T-Kernel mutex protects the snapshot. Neither task holds it during sensor I/O, task delays or USB logging. No application FreeRTOS objects are created. There are no application ISRs in this polling implementation.

The logger prints the **observed** processor IDs on every new sample; these provide runtime evidence of affinity after flashing. GPIO/I2C access is bounded and polled through ESP-IDF low-level GPIO definitions and ROM timing routines, rather than the SDK's FreeRTOS-dependent I2C driver. I2C has a nominal maximum rate of approximately 100 kHz, with actual rate lower due to software overhead; a stuck clock is bounded to 2 ms. Ultrasonic edges are polled using the pinned CPU's cycle counter with a 30 ms timeout. Hardware timing/accuracy under load remains to be measured.

## Reading/status behaviour

Readings are obtained independently. A missing or failed sensor yields JSON `null`, with a separate status and I2C error. Zero lux remains a valid number. Failed transactions never reuse earlier successful values. All status strings are immutable, and the full sample is copied under the mutex before output.

The acquisition task waits 2000 ms **after** each measurement cycle, so the actual interval includes conversion and timeout time. USB output is through the native USB-Serial-JTAG channel; no external UART adapter is needed. If the host is disconnected, bounded transmit retries drop output rather than block sensor acquisition indefinitely.

## Validation boundary

Build and link checks do not establish sensor wiring, valid measurements, SMP runtime behaviour or course acceptance. See `VALIDATION.md` for actual checks run and the remaining on-board test plan. The existing project files remain unchanged; this is the sensor bring-up migration, not a claimed migration of the entire Pico IoT application.
