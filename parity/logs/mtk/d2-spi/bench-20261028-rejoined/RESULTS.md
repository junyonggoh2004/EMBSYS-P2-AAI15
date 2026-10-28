# D2 SPI loopback benchmark

- Date: 2026-10-28T13:34:57.846803+08:00
- HAL base commit: `8ee7b6f8c9bfcb46804f7bad5c1dbef62feb9348` (check working-tree status for benchmark sketch/script changes)
- Board/port: ESP32-S3 Super Mini / HW-747 / COM5
- Fixture: GPIO12 MOSI to GPIO13 MISO; TFT disconnected
- HAL: µT-Kernel SPI2 register-level HAL
- Clock/mode: 10000000 Hz / 0
- Attempts: 1000 per size after 10 warm-ups
- Result: PASS
- Observed resets: 0 (a complete BEGIN-to-END capture; an interrupted run fails instead of producing a PASS report)
- Free heap: 96648 B before; 96648 B after in the µT-Kernel firmware

| Bytes | Median µs | p95 µs | p99 µs | Max µs | B/s | HAL errors | Timeouts | Mismatch transactions | Mismatch bytes | Transaction error rate | Byte error rate |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 36.0 | 36.0 | 36.0 | 36 | 225345.76 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 16 | 44.0 | 44.0 | 44.0 | 45 | 365697.57 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 32 | 58.0 | 59.0 | 59.0 | 105 | 548640.4 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 64 | 89.0 | 90.0 | 90.0 | 90 | 718374.68 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 128 | 153.0 | 153.0 | 153.0 | 198 | 837422.31 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 256 | 280.0 | 281.0 | 281.0 | 289 | 913333.24 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 512 | 535.0 | 536.0 | 536.0 | 570 | 956587.37 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 1024 | 1045.0 | 1046.0 | 1046.0 | 1077 | 980229.8 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |

Throughput counts payload bytes in one direction and divides by the sum of measured HAL call times. The host CSV contains all 8,000 transaction latencies; serial output occurs after each size's timed loop.
