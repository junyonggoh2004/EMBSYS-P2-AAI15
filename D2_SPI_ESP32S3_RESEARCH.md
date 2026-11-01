# D2 Deep Dive - SPI Support on ESP32-S3

Updated: 2026-09-26

## 1. Deliverable interpretation

The project requirement is to add SPI to the ESP32-S3 low-level driver layer behind the same abstraction and registry used by GPIO, UART, and I2C. Runtime configuration must cover the SPI host, SCLK, MOSI, MISO, chip select, frequency, and mode. Both transmit and receive failures must be reported explicitly. Evidence must include initialization, known-pattern integrity, and latency, throughput, and error rate for payloads from 8 to 1024 bytes.

This creates three separate claims that must all be demonstrated:

1. **Architectural claim:** application code reaches SPI devices through the common registry.
2. **Correctness claim:** received bytes are verified rather than assumed valid.
3. **Performance claim:** the implementation is measured repeatably on the ESP32-S3.

The TFT alone can support the first claim and much of the transmit-performance claim. It cannot prove receive integrity if its SDO/MISO signal is unavailable.

## 2. Current repository baseline

The current repository is a Pico 2 W application, not an ESP-IDF application.

Relevant existing structure:

- `project_v3/include/bus/bus_if.h` defines `bus_t` with `init_cfg`, `probe`, `poll_once`, `stream_start`, and `stream_stop`.
- `project_v3/bus/bus_if.c` registers only `bus_i2c`, `bus_uart`, and `bus_gpio`.
- `project_v3/include/bus/bus_common.h` defines `proto_i2c`, `proto_uart`, `proto_gpio`, and `proto_mqtt`, but no SPI protocol.
- `project_v3/application/config_parser.c` parses runtime keys for I2C, UART, GPIO, and MQTT, but no SPI keys.
- `bus_common.h` directly includes `pico/time.h`, so the common layer is not yet platform-neutral.

Design gaps that D2 must either fix or coordinate with D1:

- No `proto_spi` or `bus_spi` registry entry.
- No general `transceive` operation for device commands and responses.
- No `deinit` operation, which is required for safe runtime reconfiguration.
- No documented common error model.
- No shared-bus ownership model for multiple devices on one SPI host.
- No duplicate-pin validation across bus types.
- Scheduler reconfiguration can discard contexts without visibly releasing platform resources.

## 3. Scope boundary

Use four layers:

```text
Application / scheduler
        |
Common bus and device contract
        |
ESP32-S3 SPI transport
        |
Device driver: TFT, EEPROM, sensor, loopback fixture
```

The SPI transport owns:

- host selection and bus initialization
- SCLK, MOSI, and MISO routing
- CS, clock frequency, mode, queue depth, and duplex configuration
- transaction submission, timeout, and ESP-IDF error translation
- DMA buffer requirements
- device removal and shared-bus lifetime

The device driver owns:

- register addresses and commands
- expected response lengths and values
- controller-specific initialization
- data interpretation, checksums, and device identity

For the TFT, ST7789P3/NV3023B commands, offsets, rotation, inversion, RGB/BGR order, and gamma settings must not be embedded in the generic SPI backend.

## 4. Recommended common interface

The current `poll_once` operation is useful for sensors but insufficient as the only SPI operation. A practical version-two interface is:

```c
typedef enum {
    BUS_OK = 0,
    BUS_ERR_INVALID_CFG = -1,
    BUS_ERR_NO_MEMORY = -2,
    BUS_ERR_BUSY = -3,
    BUS_ERR_TIMEOUT = -4,
    BUS_ERR_IO = -5,
    BUS_ERR_VERIFY = -6,
    BUS_ERR_UNSUPPORTED = -7,
    BUS_ERR_NOT_PRESENT = -8
} bus_status_t;

typedef struct {
    const char *name;
    bus_status_t (*init_cfg)(const sensor_cfg_t *cfg, void **ctx_out);
    bus_status_t (*probe)(void *ctx);
    int (*poll_once)(void *ctx, uint8_t *buf, size_t cap);
    int (*transceive)(void *ctx,
                      const uint8_t *tx, size_t tx_len,
                      uint8_t *rx, size_t rx_len,
                      uint32_t timeout_ms);
    bus_status_t (*stream_start)(void *ctx);
    bus_status_t (*stream_stop)(void *ctx);
    void (*deinit)(void *ctx);
} bus_t;
```

Recommended return convention for byte operations:

- positive: number of valid received bytes
- zero: successful transmit-only operation
- negative: a specific `BUS_ERR_*` value

Do not use a zero-filled receive buffer to signal failure. A sample should carry at least `status`, `timestamp`, `fresh`, `length`, and `data` so a stale or failed read cannot appear valid.

## 5. Runtime configuration contract

Minimum transport keys:

```ini
proto=spi
name=tft0
spi.host=spi2
spi.sclk=12
spi.mosi=11
spi.miso=-1
spi.cs=10
spi.hz=10000000
spi.mode=0
spi.queue_depth=4
spi.duplex=full
```

Useful device-level keys belong outside the transport configuration:

```ini
device.type=tft
tft.controller=st7789p3
tft.width=76
tft.height=284
tft.dc=7
tft.reset=6
tft.backlight=5
tft.x_offset=...
tft.y_offset=...
tft.rotation=...
tft.rgb_order=rgb
tft.invert=...
```

Validate the entire candidate configuration before modifying a working device. Validation should reject:

- unsupported host values
- mode outside 0-3
- zero, negative, or excessive frequency
- invalid or unavailable pins
- duplicate pins within the device
- conflicts with USB, boot, flash/PSRAM, or another active device
- MISO omitted for an operation that requires receive data
- queue depth outside the supported range
- transfer length beyond the configured bus/device maximum

If validation or initialization fails, keep the previous working configuration active.

## 6. ESP-IDF implementation model

ESP-IDF separates a physical bus from devices on that bus:

1. Call `spi_bus_initialize()` once for each unique host and SCLK/MOSI/MISO configuration.
2. Call `spi_bus_add_device()` for each CS-bound device and store its `spi_device_handle_t`.
3. Submit synchronous interrupt-driven transactions with `spi_device_transmit()`, queued transactions with `spi_device_queue_trans()`, or polling transactions with `spi_device_polling_transmit()`.
4. Remove a device with `spi_bus_remove_device()`.
5. Free the bus only after all devices have been removed.

The bus manager therefore needs a table keyed by host and bus pins with:

- initialized state
- bus configuration
- device count/reference count
- synchronization primitive
- maximum transfer size

Each SPI device context stores:

- a reference to the shared bus entry
- `spi_device_handle_t`
- CS, clock, mode, duplex, queue depth, and device-specific timing
- reusable transaction descriptors and buffers where appropriate

Use `SPI_DMA_CH_AUTO` for large transfers. Buffers used directly by DMA must satisfy ESP-IDF's memory and alignment requirements; reusable DMA-capable buffers avoid hidden per-transaction copying. Small transfers of at most 32 bits can use the inline `tx_data` and `rx_data` fields.

Use only one transaction style per device in the first implementation. Mixing polling and interrupt transactions adds sequencing constraints and is unnecessary until measurements justify it.

Primary reference: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/spi_master.html>

## 7. Pin allocation and the Super Mini

Provisional TFT mapping:

| Signal | GPIO | Note |
| --- | ---: | --- |
| SCLK | 12 | SPI2 IO_MUX SCLK |
| MOSI | 11 | SPI2 IO_MUX MOSI |
| MISO | `-1` for TFT | Most small TFT breakouts are write-only |
| CS | 10 | SPI2 IO_MUX CS0 |
| DC | 7 | Ordinary GPIO |
| reset | 6 | Ordinary GPIO |
| backlight | 5 | Only if BL is a logic/PWM input |

For loopback, GPIO13 is the SPI2 IO_MUX MISO pin and can be connected temporarily to GPIO11 MOSI. However, GPIO13 is also provisionally reserved for the future HC-SR04 Echo input. The configuration validator must reject simultaneous ownership. An alternative free GPIO can be used through the GPIO matrix; ESP-IDF documents GPIO-matrix routing as suitable at SPI clocks of 80 MHz or below.

Avoid GPIO19/20 while native USB is in use. Keep GPIO0 available for boot control and verify GPIO48 use on the exact board. Generic “ESP32-S3 Super Mini” boards are not a single controlled design, so printed labels and module flash/PSRAM wiring must be checked.

## 8. TFT integration

The 76x284 TFT is a strong D2 application device because a full RGB565 frame is 43,168 bytes. It exercises initialization, large transfers, DMA, chunking, CS/DC control, and concurrency.

Use a deterministic diagnostic image containing:

- solid primary-color bars
- a one-pixel border
- checkerboard blocks
- row/column markers
- a frame counter

This exposes incorrect offsets, RGB/BGR order, inversion, mode, dropped chunks, and stale frames. Start at mode 0 and 10 MHz, then increase the clock only after the panel is stable.

ESP-IDF includes an ST7789 panel driver, but its documentation warns that manufacturer-specific initialization may still be required. NV3023B needs its own initialization table even though the generic SPI transport remains unchanged.

Primary reference: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/index.html>

The TFT does **not** prove RX integrity when SDO/MISO is absent. A successful SPI transmit also does not prove a display is connected because SPI has no acknowledgement phase.

## 9. Integrity fixture

### Option A - documented MOSI-to-MISO loopback

Wire MOSI to MISO and perform simultaneous full-duplex transfers. This is the cheapest fixture and directly verifies the controller transmit/receive path.

Strengths:

- no additional active peripheral
- exact byte-for-byte comparison
- covers every required payload size

Limitations:

- does not validate device command timing or chip-select behavior
- can pass even when the intended external device is absent

### Option B - readable SPI peripheral

Use an EEPROM, FRAM, flash device, or another documented peripheral. Write a known pattern, read it back, and verify its identity/status where supported.

Strengths:

- validates CS and real device protocol behavior
- supports presence detection and device-level error cases

Limitations:

- requires additional hardware and a device driver
- writes may have endurance or page-boundary constraints

Recommended evidence is both the TFT and loopback. Add a readable peripheral only if one is readily available.

## 10. B2 benchmark protocol

Test payloads of exactly 8, 16, 32, 64, 128, 256, 512, and 1024 bytes.

For every payload size and selected clock rate:

1. Record firmware commit, ESP-IDF version, board/module, CPU frequency, SPI host, pins, mode, clock, DMA setting, and transaction style.
2. Generate a deterministic sequence containing a transfer counter and pseudo-random bytes.
3. Perform at least 10 warm-up transactions.
4. Perform at least 1,000 measured transactions.
5. Measure each operation with `esp_timer_get_time()`.
6. Compare every received byte against the expected pattern.
7. Record median, p95, p99, and maximum latency.
8. Record total payload bytes divided by elapsed time as effective throughput.
9. Record attempts, ESP-IDF errors, timeouts, mismatched transactions, mismatched bytes, and resets.
10. Repeat a representative subset while I2C acquisition, TFT refresh, and ML inference are active.

Suggested CSV columns:

```text
run_id,firmware,idf,board,host,mode,clock_hz,dma,method,
payload_bytes,iteration,latency_us,status,mismatch_bytes
```

Report two error rates:

```text
transaction_error_rate = failed_or_mismatched_transactions / attempted_transactions
byte_error_rate        = mismatched_received_bytes / received_bytes
```

Do not combine transport errors and data mismatches into a single unexplained figure.

## 11. Negative and stress tests

Required negative tests:

- invalid host
- invalid mode
- invalid frequency
- invalid/reserved pin
- duplicate GPIO assignment
- receive requested with MISO disabled
- disconnected MOSI-MISO loopback
- deliberately corrupted expected pattern
- timeout or queue exhaustion
- reconfiguration failure with previous configuration retained

Stress tests:

- repeated initialize/deinitialize cycles
- two devices sharing one SPI bus
- TFT transfers concurrent with I2C sampling
- TFT transfers concurrent with ML inference
- sustained run long enough to observe heap loss, missed samples, errors, or resets

## 12. Proposed acceptance criteria

The requirements state that numerical pass/fail thresholds must be agreed with the supervisor. The following are proposed starting points, not approved requirements:

- all eight payload sizes complete successfully
- zero byte mismatches in the measured loopback run
- zero valid-looking samples emitted for injected failures
- invalid configuration leaves the previous device operational
- no progressive heap loss across 100 initialize/deinitialize cycles
- p95 latency and throughput reported without a mandatory target until a device/use-case requirement is agreed
- concurrent SPI activity produces no unexplained sensor or inference failures

## 13. Implementation sequence

1. Coordinate with D1 on a pinned ESP-IDF version and platform-neutral time/error types.
2. Add `proto_spi`, configuration fields, parser validation, and registry entry.
3. Add `deinit` and `transceive` semantics to the common contract.
4. Implement the shared SPI bus manager and one device context.
5. Implement loopback initialization and known-pattern tests.
6. Build the B2 benchmark logger.
7. Bring up the TFT through the same transport.
8. Add safe runtime reconfiguration and rollback.
9. Run contention and long-duration tests with D4 inference enabled.

## 14. Evidence checklist

- [ ] Exact board/module and ESP-IDF version recorded
- [ ] Common registry reaches an SPI device
- [ ] All required runtime fields demonstrated
- [ ] Initialization success and failure demonstrated
- [ ] Known-pattern RX integrity demonstrated
- [ ] 8-1024-byte benchmark completed
- [ ] Latency distribution, throughput, and both error rates reported
- [ ] TFT diagnostic pattern demonstrated
- [ ] Invalid configuration rollback demonstrated
- [ ] Concurrent SPI/I2C/ML run measured
- [ ] Limitations, including write-only TFT behavior, stated

## 15. Decisions still required

1. Exact ESP32-S3 module, flash size, PSRAM, and board pinout.
2. ST7789P3 or NV3023B controller and required panel offsets.
3. TFT backlight electrical behavior.
4. Loopback only or an additional readable SPI peripheral.
5. Approved numerical acceptance thresholds.
6. Whether the common interface may be revised directly or must preserve source compatibility.

