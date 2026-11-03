# D2 SPI loopback benchmark

- Date: 2026-10-17T14:58:58.524201+08:00
- HAL base commit: `efd5c9a8e3e427f4717612c2703d04dda3ec85de` (check working-tree status for benchmark sketch/script changes)
- Board/port: ESP32-S3 Super Mini / HW-747 / COM5
- Fixture: GPIO12 MOSI to GPIO13 MISO; TFT disconnected
- HAL: EdgeS3 SPIClass HAL
- Clock/mode: 10000000 Hz / 2
- Attempts: 1000 per size after 10 warm-ups
- Result: PASS
- Observed resets: 0 (a complete BEGIN-to-END capture; an interrupted run fails instead of producing a PASS report)
- Free heap: 323732 B before; 323764 B after in the dedicated benchmark sketch

| Bytes | Median µs | p95 µs | p99 µs | Max µs | B/s | HAL errors | Timeouts | Mismatch transactions | Mismatch bytes | Transaction error rate | Byte error rate |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 35.0 | 35.0 | 39.0 | 40 | 229502.55 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 16 | 52.0 | 56.0 | 57.0 | 58 | 304785.13 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 32 | 87.0 | 92.0 | 92.0 | 92 | 364240.67 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 64 | 158.0 | 162.0 | 163.0 | 163 | 403462.21 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 128 | 299.0 | 303.0 | 304.0 | 304 | 426500.33 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 256 | 584.0 | 585.0 | 587.0 | 587 | 439187.16 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 512 | 1149.0 | 1153.0 | 1153.0 | 1154 | 445502.51 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 1024 | 2281.0 | 2285.0 | 2285.0 | 2286 | 448816.9 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |

Throughput counts payload bytes in one direction and divides by the sum of measured HAL call times. The host CSV contains all 8,000 transaction latencies; serial output occurs after each size's timed loop.
