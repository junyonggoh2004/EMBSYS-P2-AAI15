# D2 SPI loopback benchmark

- Date: 2026-10-17T14:59:20.163770+08:00
- HAL base commit: `efd5c9a8e3e427f4717612c2703d04dda3ec85de` (check working-tree status for benchmark sketch/script changes)
- Board/port: ESP32-S3 Super Mini / HW-747 / COM5
- Fixture: GPIO12 MOSI to GPIO13 MISO; TFT disconnected
- HAL: EdgeS3 SPIClass HAL
- Clock/mode: 20000000 Hz / 0
- Attempts: 1000 per size after 10 warm-ups
- Result: PASS
- Observed resets: 0 (a complete BEGIN-to-END capture; an interrupted run fails instead of producing a PASS report)
- Free heap: 323732 B before; 323764 B after in the dedicated benchmark sketch

| Bytes | Median µs | p95 µs | p99 µs | Max µs | B/s | HAL errors | Timeouts | Mismatch transactions | Mismatch bytes | Transaction error rate | Byte error rate |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 31.0 | 32.0 | 36.0 | 37 | 255599.22 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 16 | 45.0 | 49.0 | 50.0 | 51 | 351710.19 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 32 | 74.0 | 78.0 | 78.0 | 79 | 433234.5 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 64 | 130.0 | 134.0 | 135.0 | 135 | 490380.12 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 128 | 243.0 | 248.0 | 248.0 | 248 | 524745.01 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 256 | 469.0 | 473.0 | 473.0 | 473 | 544123.99 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 512 | 925.0 | 925.0 | 926.0 | 926 | 553837.43 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 1024 | 1832.0 | 1833.0 | 1833.01 | 1835 | 559166.23 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |

Throughput counts payload bytes in one direction and divides by the sum of measured HAL call times. The host CSV contains all 8,000 transaction latencies; serial output occurs after each size's timed loop.
