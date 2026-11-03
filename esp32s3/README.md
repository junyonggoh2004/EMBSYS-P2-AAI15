# ESP32-S3 Arduino bring-up

Open `esp32s3.ino` in Arduino IDE. This sketch uses only the Arduino-ESP32 core and repeatedly prints `Hello from ESP32-S3` at 115200 baud so the message is visible even when Serial Monitor opens after boot. It does not include any Pico framework code or claim feature parity.

Follow the ESP32-S3 section of [SETUP.md](../SETUP.md) for the pinned Arduino IDE 2.3.10 and Arduino-ESP32 core 3.3.12 baseline, board options, and Arduino IDE **Verify/Compile → Upload → Serial Monitor** procedure. A local run on 2026-10-13 compiled and uploaded the sketch to the HW-747 style ESP32-S3 Super Mini using the `ESP32S3 Dev Module` profile and showed the expected line on COM6 at 115200 baud; see the [Task 9 result record](../parity/logs/9-arduino-bringup.txt). Other machines still need to reproduce the compile for Task 7, and the final integration pin layout for Task 8 remains open.

On branch `s3-port-d2`, this bring-up remains the regression smoke test while Brendan and Jy add SPI behind the shared bus abstraction. The minimal sketch must not become a one-off SPI implementation. D2 scope, proposed configuration, hardware proof, benchmarks, and completion criteria are maintained in [D2_SPI_TODO.md](../D2_SPI_TODO.md).

Future platform code can live under `src/`, with shared application code behind the HAL. Add that directory when there is actual port code to compile; keep the current bring-up sketch minimal.
