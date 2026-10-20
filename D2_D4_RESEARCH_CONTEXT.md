# D2 and D4 Research Context

Updated: 2026-09-26

## 1. Purpose and evidence boundaries

This document is the shared research context for the two team members working on:

- **D2 - Add SPI Support on ESP32-S3**
- **D4 - Improve Embedded ML Efficiency**

It consolidates the project requirements, the current repository, the shared ChatGPT research conversation, and primary ESP-IDF/Espressif documentation. Statements about intended architecture or proposed tests are plans, not evidence that the feature already works. Final D2 and D4 results must be measured on the physical ESP32-S3.

Primary project sources:

- Project requirements: `C:/Users/brend/Downloads/ESP_P2_AAI15.pdf`
- Shared research conversation: <https://chatgpt.com/s/cx_6ab235ca82448191a1939ace6c4e7792>
- Current Pico implementation: this repository

Primary platform sources:

- ESP-IDF SPI master driver: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/spi_master.html>
- ESP-IDF LCD framework: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/index.html>
- Espressif TFLite Micro: <https://github.com/espressif/esp-tflite-micro>
- ESP-NN optimized kernels: <https://github.com/espressif/esp-nn>
- ESP-IDF partition API: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/partition.html>
- ESP-IDF heap measurement: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/mem_alloc.html>
- ESP Timer: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/esp_timer.html>

## 2. Confirmed hardware context

The target board is a generic ESP32-S3 Super Mini using native USB. Because boards sold under this name vary, the printed pin labels and module marking must be checked before power is applied.

| Device | Controller or function | Interface | Current readiness |
| --- | --- | --- | --- |
| TCRT5000 MH-Sensor | Infrared reflectance plus comparator | GPIO DO; optional ADC AO | Can be tested now at 3.3 V |
| Grove Digital Light Sensor V1.1 | TSL2561 | I2C, address `0x29` | Can be tested now with Grove-to-Dupont adapter |
| HC-SR04 | Ultrasonic ranging | GPIO trigger plus timed Echo capture | Must wait for Echo level conversion |
| 76x284 IPS TFT | ST7789P3 or NV3023B | Four-wire SPI | Can be tested now if breakout and backlight wiring are confirmed |
| SPI integrity fixture | MOSI-MISO loopback or SPI memory | Full-duplex SPI | Still required for D2 RX evidence |

Provisional pin allocation:

| ESP32-S3 resource | Assignment |
| --- | --- |
| GPIO1 / ADC1_CH0 | TCRT5000 AO |
| GPIO4 | TCRT5000 DO |
| GPIO8 | TSL2561 SDA |
| GPIO9 | TSL2561 SCL |
| SPI2_HOST | TFT SPI host |
| GPIO12 | TFT SCLK |
| GPIO11 | TFT MOSI |
| GPIO10 | TFT CS |
| GPIO7 | TFT DC |
| GPIO6 | TFT reset |
| GPIO5 | TFT backlight only if it is a logic/PWM input |
| GPIO2 | Future HC-SR04 trigger |
| GPIO13 | Future HC-SR04 Echo through a divider/level shifter |

Avoid GPIO19 and GPIO20 because native USB normally uses them. Keep GPIO0 free for boot control and GPIO48 free for the common onboard RGB LED arrangement. The allocation remains provisional until it is checked against the exact board.

Electrical constraints:

- Power the TCRT5000 and Grove light sensor from 3.3 V for initial tests.
- Do not connect the nominal 5 V HC-SR04 Echo directly to ESP32-S3 GPIO.
- All devices must share ground.
- Do not drive the TFT backlight from a GPIO unless the breakout documentation says its BL input is logic/PWM compatible.

## 3. Current repository baseline

The repository currently targets `pico2_w` and links Pico SDK libraries. It is not an ESP-IDF application yet.

Useful existing architecture:

- `project_v3/include/bus/bus_if.h` defines a common `bus_t` interface.
- `project_v3/bus/bus_if.c` registers I2C, UART, and GPIO drivers.
- `project_v3/application/scheduler.c` selects drivers with `bus_lookup()` and calls `poll_once()`.
- Runtime sensor configuration already reaches the scheduler through `app_cfg_t`.
- TFLite models can be transferred over MQTT with normalization and threshold parameters.

Known portability and functionality gaps:

- `bus_common.h` directly includes `pico/time.h`.
- GPIO, I2C, UART, timers, flash, Wi-Fi, and TFLite code directly use Pico SDK APIs.
- `proto_spi` and SPI configuration fields do not exist.
- The bus interface has no `deinit` method and no general `transceive` operation.
- Scheduler reset clears structures without releasing dynamically allocated bus contexts.
- Unknown protocol values can fall through to the GPIO name in `proto_name()`.
- The model store assumes a Pico with 4 MB flash and a hard-coded final 256 KB region.
- The ESP32-S3 TFLite and partition-storage implementations do not exist.
- The inference manager understands only autoencoder MSE and SVDD distance. It does not interpret classifier, regression, or forecast outputs.

## 4. D2 research and implementation direction

### 4.1 Required result

SPI must appear behind the same registry and scheduler path as the existing buses. Runtime configuration must cover host selection, SCLK, MOSI, MISO, CS, clock frequency, and SPI mode. A failed transaction or invalid result must not be published as a valid sample.

### 4.2 Layering

Keep three concerns separate:

```text
Application and scheduler
        |
Common bus/device contract
        |
ESP32-S3 SPI transport
        |
Device driver (TFT, SPI memory, or another peripheral)
```

The SPI layer owns host, pins, clock, mode, DMA, transaction execution, and transport errors. The TFT driver owns ST7789P3/NV3023B commands, panel offsets, rotation, pixel order, inversion, and display initialization.

### 4.3 Proposed common API changes

Add:

- `proto_spi`
- `bus_spi` registry entry
- a `deinit` operation
- a `transceive` operation when a device needs arbitrary TX/RX outside `poll_once`
- platform-neutral time helpers
- documented status semantics

Recommended result convention:

```text
positive value = valid received byte count
zero           = successful operation with no received data
negative value = explicit BUS_ERR_* code
```

Each published sample should also carry timestamp, freshness, and validity. Transport success is not proof that an SPI peripheral is present because SPI has no acknowledgement mechanism. Device-level validation should use an ID register, status field, CRC/checksum, expected response, or application-level visual check where appropriate.

### 4.4 Runtime SPI configuration

Minimum keys:

```ini
proto=spi
spi.host=spi2
spi.sclk=12
spi.mosi=11
spi.miso=-1
spi.cs=10
spi.hz=10000000
spi.mode=0
spi.tx=...
spi.rx_len=...
```

Useful extensions include duplex mode, command/address/dummy bit lengths, bit order, queue depth, and device-specific post-transaction delay. Configuration must be validated completely before replacing a working device.

### 4.5 ESP-IDF design

- Initialize each unique SPI host and pin tuple once with `spi_bus_initialize()`.
- Register each CS-bound peripheral with `spi_bus_add_device()`.
- Store `spi_device_handle_t` in the per-device context.
- Reference-count shared buses and free them only after their final device is removed.
- Use `SPI_DMA_CH_AUTO` for large transfers and DMA-capable reusable buffers where required.
- Compare polling and interrupt/queued transfers during benchmarking; choose the production path using measured workload behaviour.
- Do not use SPI0/1 as the general-purpose application bus.

The TFT is strong evidence for initialization, configuration, sustained TX, DMA, and application-level operation. It is not sufficient for RX integrity when MISO/SDO is absent. Use loopback or a readable SPI peripheral for that part of D2.

### 4.6 B2 benchmark method

Test payload sizes of 8, 16, 32, 64, 128, 256, 512, and 1024 bytes.

For each size and selected clock rate:

1. Perform at least 10 warm-up transfers.
2. Perform at least 1,000 measured transfers.
3. Use a deterministic incrementing or pseudo-random pattern.
4. Verify every received byte or peripheral response.
5. Record median, p95, and maximum latency.
6. Record effective payload throughput.
7. Record attempts, mismatches, transport errors, and error rate.
8. Repeat a selected subset while I2C acquisition and ML inference are active.

Required negative tests:

- invalid SPI host, pin, mode, and frequency
- disconnected or incorrect MISO loopback
- corrupted expected data
- missing/unresponsive peripheral where device-level validation is available
- attempted duplicate GPIO assignment

## 5. D4 research and implementation direction

### 5.1 Models found in the repository

| Model | File size | Intended behaviour | Current integration |
| --- | ---: | --- | --- |
| `autoencoder_int8 (2).tflite` | 16,440 bytes | Reconstruction-error anomaly detection | Baked in and supported with MSE |
| `svdd_encoder_fp32.tflite` | 2,272 bytes | One-class latent-distance anomaly detection | Uploadable and supported with distance mode |
| `forecast_model_int8.tflite` | 4,104 bytes | Temporal prediction or prediction-error detection | File exists; no dedicated scoring path or code reference |
| Rule engine | N/A | Deterministic event detection | Integrated; required D6 baseline |

The current input cache supports 16 features arranged as four slots of four. The tensor arena is fixed at 64 KB. The wrapper accepts FP32 and INT8 tensor I/O but converts through float arrays. Thirty TFLite operators are registered regardless of the loaded model.

### 5.2 Model candidates

Recommended experiment set:

1. Existing INT8 autoencoder as the current deployable baseline.
2. Matched FP32 and fully INT8 SVDD encoders to measure quantization fairly.
3. INT8 logistic/one-layer classifier as the minimum-resource supervised baseline.
4. Small INT8 MLP classifier as the preferred final event model.
5. A small 1D CNN/TCN only if compact static features cannot distinguish ambiguous temporal events.

The final people-flow classes should align with D6 ground truth, for example entry, exit, incomplete/turn-around, and false/object event. An anomaly model only reports unusual behaviour; it does not directly solve this classification task.

### 5.3 Compact feature direction

Prefer a short event feature vector over a long raw sensor window where accuracy allows. Candidate features include:

- start and end target position
- signed displacement and boundary-crossing order
- mean and peak speed
- event duration and dwell time
- direction reversals
- minimum ultrasonic distance and time below threshold
- TCRT5000 crossing state or reflectance statistic
- ambient light context
- target count and valid-sample ratio
- per-sensor age/freshness flags

Training and firmware must use the same feature order, units, normalization, missing-data behaviour, and version.

### 5.4 ESP32-S3 inference path

- Replace Pico-specific `pico-tflmicro` with Espressif `esp-tflite-micro` for the ESP target.
- Enable ESP-NN optimized kernels and record the exact component versions.
- Register only the operators required by the selected models.
- Add classifier output handling: class argmax, confidence, labels, and optional rejection threshold.
- Add forecast-error handling only if the forecast model remains in scope.
- Reuse one tensor arena for one active model at a time.
- Prefer internal SRAM for the active arena when it fits; measure PSRAM separately if used.
- Size the arena from measured `arena_used_bytes()` plus a documented margin.
- Avoid unnecessary per-element float conversion on the final fully integer path.

### 5.5 Configurable model storage

Replace the hard-coded Pico flash address with an ESP-IDF data partition. Prefer two slots:

```text
active model A
candidate model B -> verify -> allocate -> test -> atomically activate
```

Each model should have a manifest containing:

- model ID and version
- byte length and CRC32 or SHA-256
- TFLite schema version
- input/output shapes and tensor types
- feature schema/version
- quantization scales and zero points
- required operator list
- required arena size
- score mode or class labels
- thresholds

If candidate validation fails, retain the previous active model and report the error.

### 5.6 Dataset and ground truth

This is a critical shared dependency for D4 and D6. Define before training:

- event class definitions
- sensor placement and sampling configuration
- start/end rules for an event window
- subject/object variation
- ambiguous and false-event cases
- train/validation/test split by trial or subject, not random adjacent samples
- treatment of missing, stale, saturated, and invalid sensor data

Repeated reads of an unchanged TSL2561 integration result are not independent samples. Preserve timestamps, sample age, and validity flags. Do not replace missing data with zero unless zero is explicitly accompanied by a validity feature and the training pipeline uses the same convention.

### 5.7 B5 benchmark method

For every candidate, measure on the same physical ESP32-S3 with a fixed CPU frequency, build configuration, sensor/communications state, and held-out event set.

| Metric | Required measurement |
| --- | --- |
| Model size | Exact `.tflite` bytes |
| Arena use | `arena_used_bytes()` |
| Runtime memory | Heap before load, after allocation, minimum free heap, largest block |
| Latency | Warm-up then at least 1,000 invokes; median, p95, p99, maximum |
| Classification | Confusion matrix, per-class precision/recall, macro-F1 |
| Use-case performance | Entry/exit accuracy, false events, occupancy-count error |
| Reliability | Allocation failures, invoke failures, resets, stale-input decisions |

Do not compare different architectures and attribute their size difference solely to quantization. Export FP32 and INT8 versions of the same architecture and evaluate them on the same data.

## 6. D2-D4 integration tests

D2 and D4 share CPU time, internal RAM, DMA-capable memory, scheduler time, logging, and MQTT bandwidth. Test them together rather than only in isolation.

Integration scenarios:

- SPI TFT refresh while inference runs.
- SPI benchmark traffic while TSL2561 I2C sampling continues.
- Inference while the TFT performs chunked RGB565 transfers.
- Model upload while sensor acquisition continues, if this remains an intended runtime feature.
- Invalid SPI configuration while the active model remains operational.
- Invalid model upload while working bus configurations remain operational.

Measure whether SPI activity changes inference latency or missed-sample counts, and whether inference changes SPI p95 latency or error rate.

## 7. Open decisions

These must be resolved before interfaces and benchmarks are frozen:

1. Exact ESP32-S3 module, flash size, PSRAM presence, and board schematic/pinout.
2. Confirm whether the TFT controller is ST7789P3 or NV3023B.
3. Confirm TFT BL electrical behaviour and required panel offsets.
4. Select the bidirectional SPI integrity fixture.
5. Agree D2 numerical acceptance thresholds for error rate and performance.
6. Freeze event classes and dataset collection protocol for D4/D6.
7. Agree the permitted accuracy loss for INT8 conversion.
8. Decide whether runtime model switching is required without reboot.
9. Decide whether classification replaces or complements anomaly detection.

## 8. Suggested two-person work split

### D2 owner

- common bus contract and error codes
- ESP-IDF SPI bus manager and device contexts
- runtime SPI configuration and validation
- TFT transport integration
- loopback/peripheral B2 harness
- SPI result tables

### D4 owner

- ESP-IDF TFLite/ESP-NN integration
- ESP partition model store and manifest validation
- data collection and feature schema
- baseline and optimized model exports
- classifier/score-mode output handling
- B5 measurement harness and result tables

### Shared

- platform-neutral timing/status types
- concurrency and resource-contention tests
- reproducible ESP-IDF/component versions
- hardware wiring record
- final D2/D4 integration evidence

## 9. Immediate next actions

1. Photograph or record the ESP32-S3 module marking and TFT rear/controller marking.
2. Freeze an ESP-IDF version and create the minimal ESP32-S3 application target.
3. Define the revised platform-neutral bus API before implementing SPI.
4. Bring up the TFT at SPI mode 0 and 10 MHz with a visible diagnostic pattern.
5. Obtain or construct a safe bidirectional SPI integrity fixture.
6. Port the current autoencoder unchanged and capture the first ESP32-S3 baseline.
7. Define the people-flow labels and collect a small pilot dataset before selecting the final model architecture.

