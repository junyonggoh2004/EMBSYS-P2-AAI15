# D2 SPI flash design and hardware checkpoint

Updated 26 October 2026. The original module-free observations below are retained as history. The µT-Kernel SPI transport, physical W25Q32 device checks and benchmark have separate target evidence. A provisional D2-only event codec and non-erasing recovery scan now pass host tests and build on both targets; the application queue and D3/D6 mapping remain proposals.

The [event-record design](D2_EVENT_RECORD_DESIGN.md) records the every-processed-event scope, provisional 80-byte codec, proposed queue and failure behavior, and open D3/D6 decisions. The old 32-byte draft is retired. The codec is compiled but not called by the current µT-Kernel `ApplicationTask`, so no physical event-log or live-concurrency claim is made.

## Physical W25Q32 fixture and current result

The actual module is a W25QXX breakout with six printed pins. The top chip marking remains hard to read in the photos; its physical `0x9F` response was `EF4016`. Brendan checked the six connections before the powered test:

| Breakout | ESP32-S3 Super Mini / HW-747 | Purpose |
|---|---|---|
| `VCC` | `3V3` | 3.3 V power |
| `GND` | `GND` | Common ground |
| `CS` | `GPIO10` | Chip select |
| `SLK` | `GPIO11` | SPI clock |
| `DI` | `GPIO12` | MOSI, ESP to flash |
| `DO` | `GPIO13` | MISO, flash to ESP |

The GPIO12↔GPIO13 jumper is a **separate loopback fixture** and must be removed when the flash is attached. On the µT-Kernel firmware, the device layer probed `EF4016`, read the same bytes twice, and found the last 4 KiB (`0x3FF000–0x3FFFFF`) initially blank. Brendan approved that sector as disposable D2 scratch. Exactly 16 bytes were programmed at `0x3FF000` once; blank check, WEL/WIP status, byte-for-byte readback, ESP reset retention and full USB power-cycle retention passed without any erase. After all flash wires were removed, the same probe returned `HAL_ERR_ABSENT (-9)`. [Read-only](parity/logs/mtk/d2-spi/flash-readonly-20261025.log), [program/reset](parity/logs/mtk/d2-spi/flash-program-once-20261025.log), [cold retention](parity/logs/mtk/d2-spi/flash-cold-verify-20261025.log) and [absent-device](parity/logs/mtk/d2-spi/flash-absent-20261025.log) raw logs are retained. At this checkpoint the flash is disconnected and GPIO12↔GPIO13 is connected for loopback.

The µT-Kernel loopback suite passed 22/22 and connected faults 6/6. Five 8–1024-byte benchmark runs passed 40,000/40,000 measured transfers; the [D2 checklist](D2_SPI_TODO.md#25-october-target-benchmark-evidence) links all reports. This is transport evidence; it does not measure event logging during live sampling.

## What the fault tests established

The generic `hal_spi_transfer()` reports transport and lock errors, but a successful full-duplex clocking operation does **not** prove that a slave is present. With GPIO12-to-GPIO13 loopback removed, the production scheduler published seven 64-byte samples even though 63 bytes in each sample disagreed with the transmitted pattern. Loopback mismatch detection exists in the host tests, not in the generic firmware driver. A real SPI device driver must validate replies and report an absent/verification error rather than publish or record a good-looking value.

At 1 kHz, a 128-byte transfer with a 1 ms timeout returned `HAL_ERR_TIMEOUT` and was not published. The `SPIClass` backend checks elapsed time after a synchronous transaction, so this timeout is an error-reporting limit, not preemptive cancellation. A separate two-task HAL test confirmed that a contender waiting on the SPI mutex returns `HAL_ERR_TIMEOUT` in about 1 ms while the first transfer finishes. The latter protects other tasks from waiting indefinitely on the mutex; it does not shorten an ongoing flash operation.

## W25Q32 device layer

The generic SPI transport is device-independent. The implemented `flash/w25q32.[ch]` layer above the HAL owns command framing, chip identification, status polling, page boundaries and readback verification. Event-record integrity still needs a schema and codec. W25Q32 opcodes do not belong in `bus/spi.c`; `CFG: ok` alone does not prove that flash is physically present.

1. On open, issue JEDEC ID (`0x9F`) in one CS-held transaction. Reject all-zero/all-`0xFF` and unknown IDs; match the expected ID to the actual part marking once the module is in hand. Record the observed ID in evidence.
2. For each write, check write-enable/status, keep page programs within a 256-byte page, poll WIP with a bounded deadline, and read back the programmed bytes. A transport success without status/readback success is a failed write.
3. After D3/D6 agree the contract, store a versioned record with canonical event identity, window time, processing outcome, publication-enqueue result and CRC. On retrieval, accept only valid records and reject torn/corrupt/blank slots. The exact record size and sector layout remain open.
4. For the initial safe hardware test, use a dedicated scratch sector and preserve any pre-existing contents. Do not erase a whole chip. Record the chosen address before testing.
5. Return distinct outcomes for no device, transport/lock timeout, WIP timeout, readback mismatch, and full log. Never convert any of these to a valid event record.

## Application and queue policy proposal

The current µT-Kernel `ApplicationTask` only drains `app_mbf`; it does not yet implement occupancy or flash logging. `app_mbf` currently has capacity 8 with 64-byte items, while `bench_mbf` has capacity 16 and `runtime_send()` counts full-buffer drops. D2 must not claim a concurrent logging pass from these placeholders.

- Keep sensor acquisition and inference out of flash write calls. For every processed event, attempt MQTT publication enqueue first, then offer a log request to `ApplicationTask`, including rejected, duplicate and degraded outcomes. The publish queueing order is observable; broker acknowledgement is a separate matter. Exact mappings await D3/D6 agreement.
- Preferred design: a dedicated, typed `log_mbf` carrying the agreed record; provisional capacity 8 and enqueue timeout 0 ms. If full, drop the **newest log request**, increment a RAM diagnostic counter, and report the lost sequence. Do not block the scheduler or overwrite an earlier accepted record silently. `ApplicationTask` consumes the buffer; the existing D6-shared `app_mbf` is not repurposed. This requires teammate review before implementation.
- Flash writes may block `ApplicationTask` but must use bounded SPI and WIP deadlines. On failure, increment a failure counter, emit one rate-limited error, and continue taking later occupancy events. Never retry forever on the application task.
- Benchmark buffer: `bench_mbf` has capacity 16, zero-wait send and a drop counter; add a high-water diagnostic before concurrent performance evidence. The completed SPI transfer benchmarks use static timing arrays and a complete 8,000-sample serial/CSV capture per configuration, rather than offering every transfer to `bench_mbf`.
- Capture maximum buffer occupancy and overflow count for `app_mbf`, `bench_mbf`, `pub_mbf` and the future `log_mbf`; log the relevant existing policies, including `pub_mbf` capacity 16. For the final T-23 run, report sampling interval median/p95/p99/max, SPI mutex wait/hold times, flash write latency, each task's stack high-water mark, heap minimum, resets, and watchdog events.

## Gates still requiring people or hardware

- Team review of the queue owner, record format, retention/overflow policy, maximum approved SPI clock, and event publication semantics.
- A legible chip-top marking and team approval of the production SPI clock; the `EF4016` physical ID and 1 MHz test clock are recorded above.
- The combined flash-write + live sampling + inference run and actual event-log retrieval. The completed transport-only loopback and one-time scratch test cannot replace it.

## Teammate review questions

1. For the final fixture, confirm the still-unreadable chip-top marking, module voltage/level shifting, JEDEC ID and GPIO11/12/13/10 wiring. The first powered test and its six-wire trace are recorded above.
2. Approve whether `ApplicationTask` owns a dedicated flash-log queue or a typed extension of its current 8×64-byte `app_q`; settle the new-event drop policy and diagnostic counters.
3. Agree on record size, log capacity, sector-erasure policy, retention, and what happens when flash is full. The scratch-sector test must not erase any unknown existing contents.
4. Define an acceptable p95 and maximum sampling interval, mutex wait and hold limit, log-write latency, and minimum task-stack/heap headroom for T-23.
5. Review the migrated-build 22/22 loopback and absent-device results on the checkpointed source revision before merge. Do not sign off on FR-17/T-21 until the remaining target fault cases, including corrupted replies and mutex contention, pass.
