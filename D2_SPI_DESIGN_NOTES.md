# D2 SPI flash design checkpoint (module-free)

Date: 2026-10-17. Status: proposed design for teammate review; no W25Q32 has been attached or validated.

The provisional 32-byte event format, dedicated queue policy, failure behavior, test template, and approval fields are in [D2_EVENT_RECORD_DESIGN.md](D2_EVENT_RECORD_DESIGN.md). These are design decisions for review, not working firmware.

## What the fault tests established

The generic `hal_spi_transfer()` reports transport and lock errors, but a successful full-duplex clocking operation does **not** prove that a slave is present. With GPIO12-to-GPIO13 loopback removed, the production scheduler published seven 64-byte samples even though 63 bytes in each sample disagreed with the transmitted pattern. Loopback mismatch detection exists in the host tests, not in the generic firmware driver. A real SPI device driver must validate replies and report an absent/verification error rather than publish or record a good-looking value.

At 1 kHz, a 128-byte transfer with a 1 ms timeout returned `HAL_ERR_TIMEOUT` and was not published. The `SPIClass` backend checks elapsed time after a synchronous transaction, so this timeout is an error-reporting limit, not preemptive cancellation. A separate two-task HAL test confirmed that a contender waiting on the SPI mutex returns `HAL_ERR_TIMEOUT` in about 1 ms while the first transfer finishes. The latter protects other tasks from waiting indefinitely on the mutex; it does not shorten an ongoing flash operation.

## Proposed W25Q32 device layer

Keep the generic SPI transport device-independent. A W25Q32 layer above the HAL should own command framing, chip identification, status polling, page boundaries, readback verification, and event-record integrity. Do not place W25Q32 opcodes in `bus/spi.c` or treat `CFG: ok` as proof that flash is physically present.

1. On open, issue JEDEC ID (`0x9F`) in one CS-held transaction. Reject all-zero/all-`0xFF` and unknown IDs; match the expected ID to the actual part marking once the module is in hand. Record the observed ID in evidence.
2. For each write, check write-enable/status, keep page programs within a 256-byte page, poll WIP with a bounded deadline, and read back the programmed bytes. A transport success without status/readback success is a failed write.
3. Store a versioned fixed-size record with sequence number, monotonic timestamp, occupancy/class, and CRC. On retrieval, accept only records with valid version, length, sequence, and CRC; reject torn/corrupt/blank slots. The exact record size and sector layout await the module capacity and retention requirement.
4. For the initial safe hardware test, use a dedicated scratch sector and preserve any pre-existing contents. Do not erase a whole chip. Record the chosen address before testing.
5. Return distinct outcomes for no device, transport/lock timeout, WIP timeout, readback mismatch, and full log. Never convert any of these to a valid event record.

## Application and queue policy proposal

The current S3 `ApplicationTask` only drains `app_q`; it does not yet implement occupancy or flash logging. `app_q` currently has capacity 8 with 64-byte items, while `bench_q` has capacity 16 and `runtime_bench()` uses a nonblocking send but does not report drops. D2 must not claim a concurrent logging pass from these placeholders.

- Keep sensor acquisition and inference out of flash write calls. After the application has formed an occupancy result, enqueue its MQTT publish first, then offer a compact log request to `ApplicationTask`. The publish queueing order is observable; broker acknowledgement is a separate matter.
- Preferred design: a dedicated, typed `log_q` carrying the proposed 32-byte logical record; capacity 8 and enqueue timeout 0 ms. If full, drop the **newest log request**, increment a RAM diagnostic counter, and report the lost sequence. Do not block the scheduler or overwrite an earlier accepted record silently. `ApplicationTask` consumes the queue; the existing D6-shared `app_q` is not repurposed. This requires teammate review before implementation.
- Flash writes may block `ApplicationTask` but must use bounded SPI and WIP deadlines. On failure, increment a failure counter, emit one rate-limited error, and continue taking later occupancy events. Never retry forever on the application task.
- Benchmark queue: retain capacity 16 and enqueue timeout 0 ms, but add a dropped-record counter and queue high-water diagnostic before using it as performance evidence. A lost benchmark sample invalidates a complete-run claim.
- Capture maximum queue occupancy and overflow count for `app_q`, `bench_q`, and `pub_q`; log the relevant existing queue policies, including `pub_q` capacity 16. For the final T-23 run, report sampling interval median/p95/p99/max, SPI mutex wait/hold times, flash write latency, each task's stack high-water mark, heap minimum, resets, and watchdog events.

## Gates still requiring people or hardware

- Team review of the queue owner, record format, retention/overflow policy, maximum approved SPI clock, and event publication semantics.
- W25Q32 module marking, 3.3 V check, fixture photo, and final pin diagram.
- Physical JEDEC/read/write/readback/absent-device tests and the combined flash-write + live sampling + inference run. The existing ESP-only loopback cannot replace them.

## Teammate review questions

1. Confirm the actual W25Q32 part, its JEDEC ID, module voltage/level shifting, and the GPIO11/12/13/10 wiring before the first powered test.
2. Approve whether `ApplicationTask` owns a dedicated flash-log queue or a typed extension of its current 8×64-byte `app_q`; settle the new-event drop policy and diagnostic counters.
3. Agree on record size, log capacity, sector-erasure policy, retention, and what happens when flash is full. The scratch-sector test must not erase any unknown existing contents.
4. Define an acceptable p95 and maximum sampling interval, mutex wait and hold limit, log-write latency, and minimum task-stack/heap headroom for T-23.
5. Reproduce the 22/22 loopback test and review the disconnected-wire result on the same source revision before approving merge. Do not sign off on FR-17/T-21 until the real device rejects absent/corrupt replies.
