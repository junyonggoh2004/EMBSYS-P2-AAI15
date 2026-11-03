# D2 SPI loopback benchmark

- Date: 2026-10-25T21:35:07.707305+08:00
- HAL base commit: `1f41dac65a78ae2d62efb0ac1f559f5c997e7452` (check working-tree status for benchmark sketch/script changes)
- Board/port: ESP32-S3 Super Mini / HW-747 / COM5
- Fixture: GPIO12 MOSI to GPIO13 MISO; TFT disconnected
- HAL: µT-Kernel SPI2 register-level HAL
- Clock/mode: 10000000 Hz / 0
- Attempts: 1000 per size after 10 warm-ups
- Result: PASS
- Observed resets: 0 (a complete BEGIN-to-END capture; an interrupted run fails instead of producing a PASS report)
- Free heap: 92716 B before; 92716 B after in the µT-Kernel firmware

| Bytes | Median µs | p95 µs | p99 µs | Max µs | B/s | HAL errors | Timeouts | Mismatch transactions | Mismatch bytes | Transaction error rate | Byte error rate |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 36.0 | 36.0 | 36.0 | 36 | 225149.16 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 16 | 44.0 | 44.0 | 44.0 | 45 | 363967.24 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 32 | 58.0 | 59.0 | 59.0 | 104 | 547457.74 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 64 | 89.0 | 89.0 | 90.0 | 133 | 718382.74 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 128 | 153.0 | 153.0 | 153.0 | 197 | 837542.86 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 256 | 280.0 | 281.0 | 281.0 | 290 | 913372.34 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 512 | 535.0 | 536.0 | 536.0 | 576 | 956882.36 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 1024 | 1045.0 | 1046.0 | 1046.0 | 1074 | 979887.43 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |

Throughput counts payload bytes in one direction and divides by the sum of measured HAL call times. The host CSV contains all 8,000 transaction latencies; serial output occurs after each size's timed loop.
