# D2 SPI loopback benchmark

- Date: 2026-10-25T21:36:41.373990+08:00
- HAL base commit: `1f41dac65a78ae2d62efb0ac1f559f5c997e7452` (check working-tree status for benchmark sketch/script changes)
- Board/port: ESP32-S3 Super Mini / HW-747 / COM5
- Fixture: GPIO12 MOSI to GPIO13 MISO; TFT disconnected
- HAL: µT-Kernel SPI2 register-level HAL
- Clock/mode: 20000000 Hz / 3
- Attempts: 1000 per size after 10 warm-ups
- Result: PASS
- Observed resets: 0 (a complete BEGIN-to-END capture; an interrupted run fails instead of producing a PASS report)
- Free heap: 92716 B before; 92716 B after in the µT-Kernel firmware

| Bytes | Median µs | p95 µs | p99 µs | Max µs | B/s | HAL errors | Timeouts | Mismatch transactions | Mismatch bytes | Transaction error rate | Byte error rate |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 33.0 | 33.0 | 33.0 | 34 | 244970.45 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 16 | 37.0 | 37.0 | 37.0 | 37 | 434640.88 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 32 | 46.0 | 47.0 | 47.0 | 79 | 692221.16 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 64 | 63.0 | 64.0 | 66.0 | 116 | 1008382.18 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 128 | 101.0 | 103.0 | 103.0 | 148 | 1263062.33 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 256 | 177.0 | 178.0 | 178.0 | 223 | 1445862.07 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 512 | 329.0 | 331.0 | 331.0 | 393 | 1555654.13 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |
| 1024 | 632.0 | 636.0 | 636.0 | 679 | 1618739.44 | 0 | 0 | 0 | 0 | 0.0 | 0.0 |

Throughput counts payload bytes in one direction and divides by the sum of measured HAL call times. The host CSV contains all 8,000 transaction latencies; serial output occurs after each size's timed loop.
