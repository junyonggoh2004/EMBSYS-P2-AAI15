# Task 7 ESP32-S3 verification

Date: 2026-10-13  
Operator: Brendan  
Operating system: Windows  
Repository branch: `s3-port`  
Commit: `ea6864d6cbf53aa5bfc56247379199e1bf7c14b5`

## Environment

- Arduino IDE: 2.3.10
- Arduino-ESP32 core: 3.3.12 (`esp32 by Espressif Systems`)
- Sketch: `esp32s3/esp32s3.ino`
- Board profile: `ESP32S3 Dev Module`
- Port: `COM5`

## Board settings

- USB CDC On Boot: Enabled
- CPU Frequency: 240 MHz (WiFi)
- Core Debug Level: None
- USB DFU On Boot: Disabled
- Erase All Flash Before Sketch Upload: Disabled
- Events Run On: Core 1
- Flash Mode: QIO 80 MHz
- Flash Size: 4 MB (32 Mb)
- JTAG Adapter: Disabled
- Arduino Runs On: Core 1
- USB Firmware MSC On Boot: Disabled
- Partition Scheme: Default 4 MB with SPIFFS (1.2 MB APP / 1.5 MB SPIFFS)
- PSRAM: Disabled for the minimal bring-up sketch
- Upload Speed: 921600

## Results

- Verify: passed with no compile errors.
- Program storage: 297,242 bytes (22% of 1,310,720 bytes).
- Global variables: 22,320 bytes (6% of dynamic memory), leaving 305,360 bytes.
- Upload: passed; Arduino IDE reported `Hash of data verified` and reset via RTS.
- Serial Monitor: passed at 115200 baud; `Hello from ESP32-S3` repeats approximately once per second.

## Evidence

- `installed-versions.jpg`
- `verify-success.jpg`
- `upload-success.jpg`
- `serial-monitor-115200.jpg`

External submission or messaging was intentionally not performed.
