# D2 event-record design template

Status: **provisional design, not implemented or hardware-validated** (2026-10-17). Brendan and Jy should review it before firmware work. This document does not claim a W25Q32 test or an occupancy pipeline.

## Purpose and ownership

Record a compact occupancy event in external SPI flash after the application has attempted to queue its MQTT publication. Flash logging must not run in `SchedulerTask`, sensor acquisition, or inference. A dedicated, typed `log_q` feeds the lower-priority `ApplicationTask`, which alone owns W25Q32 log operations. The generic SPI HAL remains device-independent; a W25Q32 device layer above it owns commands, device checks, and log integrity.

“Publish first” means **attempt to enqueue** the occupancy result to the existing publisher before offering the log request. It does not assert MQTT broker delivery. A publisher-queue failure and a flash-log failure are independent, observable outcomes; neither may turn a failed reading into a valid one or stop live monitoring.

## Proposed logical record v1 (32 bytes)

Serialize fields explicitly in little-endian byte order; do not write a compiler-dependent C/C++ struct image. CRC-32 covers bytes 0–27. Values and enumeration meanings must be frozen in a header and tested with golden byte vectors before deployment.

| Byte offset | Size | Field | Notes |
|---|---:|---|---|
| 0 | 2 | Magic | Fixed marker for this log format; choose value before implementation. |
| 2 | 1 | Version | `1` for this layout. |
| 3 | 1 | Flags | At minimum, distinguish valid occupancy result and publisher enqueue outcome; reserve unused bits. |
| 4 | 4 | Event sequence | Increment when an event is formed, before queueing, so dropped requests leave detectable gaps. Define rollover handling in tests. |
| 8 | 8 | Uptime milliseconds | Monotonic time at event formation, not an unverified wall-clock timestamp. |
| 16 | 2 | Occupancy count | Unsigned; reject out-of-range application values rather than truncate. |
| 18 | 1 | Occupancy/class code | Define mapping from the actual application result. |
| 19 | 1 | Confidence | 0–100 if available; otherwise a specified unavailable sentinel. |
| 20 | 4 | Boot identifier | Identifies a reboot epoch; exact generation method needs team review. |
| 24 | 4 | Model/configuration identifier | Stable identifier or specified unavailable sentinel; do not invent model provenance. |
| 28 | 4 | CRC-32 | Covers bytes 0–27, using one documented polynomial/init/reflection convention. |

An erased slot, unsupported version, invalid flags/field values, bad CRC, or torn write is **not** a stored event. A record is reported as stored only after program completion and byte-for-byte readback (including CRC) succeed. On reboot, scan the reserved log area, accept only valid records, and report invalid/torn slots without exposing them as occupancy data. Define how scanning skips or stops at a torn slot before coding the log allocator.

This is the **logical** record format. Physical slot packing, erase-sector allocation, retention, and wrap policy remain open until the exact flash part and required event rate/retention are confirmed. Avoid assuming that a 32-byte record can be repeatedly programmed into the same flash page without checking the actual device datasheet. Never erase an unknown sector or silently wrap and overwrite retained events. For initial hardware proof, use an explicitly approved scratch sector and return a `LOG_FULL` outcome when its approved slots are exhausted.

## Queue and processing policy

| Item | Provisional choice | Required diagnostic |
|---|---|---|
| Flash-log queue | Dedicated typed `log_q`, capacity 8 records, 0 ms producer wait; `ApplicationTask` consumes FIFO | Current and high-water depth |
| Queue overflow | Drop **newest** log request; do not overwrite an accepted earlier event | Drop count and missing event sequence |
| Flash failure | No unbounded retry; finite SPI/mutex/WIP deadlines, then continue with later events | Separate absent-device, transport/lock timeout, WIP timeout, readback/CRC, and full-log counts |
| Publisher failure | Do not block the result path; still offer the log request with an explicit publish-enqueue-failed flag | Existing publisher drop/error count plus event flag |
| Reporting | Rate-limit repetitive error messages without hiding cumulative counts | One status summary with last error and counters |

Capacity 8 is a starting point, not a proven performance limit. Measure production event rate, flash write/erase worst case, queue high-water mark, and dropped events in T-23. Avoid sharing or repurposing the current `app_q` until its D6 owner approves; the dedicated queue should still be drained by `ApplicationTask` so no new high-priority flash writer is created.

## Device-layer safety contract

1. Confirm actual module marking, operating voltage and wiring before power-up. Read and validate JEDEC ID, rejecting absent/all-zero/all-`0xFF` or unexpected replies. Fix the accepted ID only after the part is identified.
2. For every program/erase, check write enable and status, respect actual page/sector boundaries, use datasheet-based bounded busy-poll deadlines, and read back programmed data. A successful SPI transfer alone does not prove a flash device exists.
3. Keep scratch-sector address and approval explicit. Do not chip-erase or erase unknown content. Return typed failure, never a synthetic successful record.
4. Recovery must reject partial/corrupt records and resume without an infinite scan or blocking the sampling task.

## Implementation and evidence checklist

- [ ] Brendan and Jy approve queue ownership, record fields/enums/CRC convention, sequence/boot-ID semantics, and MQTT ordering.
- [ ] Agree event rate, retention, scratch address, physical slot packing, full/wrap policy, and maximum acceptable sampling delay before enabling erase.
- [ ] Add portable encode/decode and CRC golden-vector tests; test blank, torn, wrong-version, bad-CRC, invalid-field, sequence rollover, and reboot scan cases without a module.
- [ ] Add mocked device tests for absent/wrong JEDEC ID, stuck busy, SPI timeout, partial program, bad readback, full log, and queue overflow. Verify no false “stored” or valid occupancy result.
- [ ] With the physical module: record part marking, 3.3 V check, fixture photo, JEDEC ID, scratch address, write/readback/reboot recovery, and disconnected-device evidence.
- [ ] During live T-23: capture event rate, queue high-water and drops, publisher queue outcome, flash latency/errors, sampling median/p95/p99/max, SPI mutex wait/hold, stack/heap, resets, and watchdog events.

## Review record (fill in before implementation)

| Decision | Agreed value / rationale | Reviewer and date |
|---|---|---|
| Occupancy/class enum and confidence sentinel | TBD | TBD |
| CRC-32 convention and magic bytes | TBD | TBD |
| Event sequence rollover and boot identifier | TBD | TBD |
| Maximum event rate and minimum retention | TBD | TBD |
| Flash part, JEDEC ID, voltage and maximum approved SPI clock | TBD; physical module required | TBD |
| Scratch address, slot layout, erase and full-log policy | TBD; no erase before approval | TBD |
| Queue capacity/drop and publication semantics | Provisional: 8, drop newest, enqueue publish first | TBD |
| T-23 latency, queue, stack and heap pass thresholds | TBD | TBD |

See [D2_SPI_DESIGN_NOTES.md](D2_SPI_DESIGN_NOTES.md) for the observed transport/fault findings and [D2_SPI_TODO.md](D2_SPI_TODO.md) for the project acceptance gates.
