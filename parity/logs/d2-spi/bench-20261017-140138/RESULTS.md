# D2 SPI loopback benchmark

- Date: 2026-10-17T14:01:43.966478+08:00
- HAL base commit: `efd5c9a8e3e427f4717612c2703d04dda3ec85de`; benchmark sketch/script were uncommitted working-tree additions during this run.
- Board/port: ESP32-S3 Super Mini / HW-747 / COM5
- Fixture: GPIO12 MOSI to GPIO13 MISO; TFT disconnected
- HAL: EdgeS3 SPIClass HAL
- Clock/mode: 10000000 Hz / 0
- Attempts: 1000 per size after 10 warm-ups
- Result: PASS
- Observed resets: 0 (complete BEGIN-to-END capture)
- Free heap: 323,732 B before; 323,764 B after. These are dedicated benchmark sketch measurements.
- Transaction error rate: 0/1,000 per size; byte error rate: 0 for every size.

| Bytes | Median µs | p95 µs | p99 µs | Max µs | B/s | HAL errors | Timeouts | Mismatch transactions | Mismatch bytes |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 35.0 | 36.0 | 40.0 | 40 | 229449.89 | 0 | 0 | 0 | 0 |
| 16 | 52.0 | 56.0 | 57.0 | 58 | 304518.29 | 0 | 0 | 0 | 0 |
| 32 | 87.0 | 92.0 | 92.0 | 93 | 364522.82 | 0 | 0 | 0 | 0 |
| 64 | 158.0 | 162.0 | 163.0 | 163 | 403426.6 | 0 | 0 | 0 | 0 |
| 128 | 299.0 | 303.0 | 304.0 | 304 | 426515.96 | 0 | 0 | 0 | 0 |
| 256 | 584.0 | 585.0 | 587.0 | 587 | 439216.55 | 0 | 0 | 0 | 0 |
| 512 | 1149.0 | 1153.0 | 1153.0 | 1154 | 445511.82 | 0 | 0 | 0 | 0 |
| 1024 | 2281.0 | 2285.0 | 2285.0 | 2286 | 448806.08 | 0 | 0 | 0 | 0 |

Throughput counts payload bytes in one direction and divides by the sum of measured HAL call times. The host CSV contains all 8,000 transaction latencies; serial output occurs after each size's timed loop.
