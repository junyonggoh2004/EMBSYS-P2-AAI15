# ESP32-S3 Arduino bring-up

Open `esp32s3.ino` in Arduino IDE. This sketch uses only the Arduino-ESP32 core and repeatedly prints `Hello from ESP32-S3` at 115200 baud so the message is visible even when Serial Monitor opens after boot. It does not include any Pico framework code or claim feature parity.

Follow the ESP32-S3 section of [SETUP.md](../SETUP.md) to pin the IDE and core versions, confirm the exact board and USB/serial settings, then use Arduino IDE **Verify/Compile → Upload → Serial Monitor**. The sketch and the physical board have **not yet been verified**. Leave Tasks 7–9 pending until their stated checks pass.

Future platform code can live under `src/`, with shared application code behind the HAL. Add that directory when there is actual port code to compile; keep the current bring-up sketch minimal.
