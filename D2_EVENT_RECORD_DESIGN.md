# D2 event-record design template

Status (25 October 2026): the W25Q32 device layer and one approved scratch write have been hardware-validated. The event-record format, queue and occupancy pipeline remain **provisional and unimplemented**. Brendan chose to log every processed event; he asked to wait for D3/D6 agreement before freezing the format.

## 25 October integration review — decision needed from D3/D6

The fetched but **unmerged** `origin/d3` branch defines an 80-byte, pointer-free application `event_record_t` in `libraries/EdgeCore/src/application/event_record.h`. It carries `event_id`, `source_id`, `source_seq`, monotonic window start/end, class/quality fields and `model_version`. Its `application_result_t` separately carries `occupancy_after`, decided class, decision source and rejection/degraded outcome. That branch integrates the application path into the older Arduino runtime; current µT-Kernel `ApplicationTask` still only drains `app_mbf`. This is an interface proposal to reconcile, not an implemented µT-Kernel D2/D6 pipeline.

The retired 32-byte draft predates that contract. It lacks `source_id` and a clearly mapped `event_id`, and its `boot identifier` has no agreed source in `origin/d3`. **Do not program an event record to flash yet.** Before freezing the on-flash format, Brendan/Jy and the D3/D6 owner should decide the following and add golden byte vectors:

| Required stored value | Candidate source | Open decision |
|---|---|---|
| Canonical identity | D3 `event_id`, `source_id`, `source_seq` | Define reset/boot uniqueness and whether all three must be stored; D6 owns de-duplication semantics. |
| Event time | D3 `window_end_us` (monotonic) | Preserve microseconds or document a bounded conversion to milliseconds and rollover. |
| Occupancy outcome | `application_result_t.occupancy_after`, result code and degraded/reason fields | Log every processed event, including rejected, duplicate and degraded outcomes; agree exact enum and signed-count encoding with D3/D6. |
| Publication outcome | Nonblocking enqueue result from the µT-Kernel publisher | Distinguish enqueue from broker delivery; preserve failure without blocking flash logging. |
| Record integrity | Version, marker and CRC | Choose a new byte layout and CRC convention; never serialize an in-memory struct directly. |

Integration order: merge/reconcile D3's shared C contracts with current `main`, port the ApplicationTask handoff to µT-Kernel, agree the mapping above, then implement the flash codec and queue. Until that approval, host-side W25Q32 command/fault tests can proceed independently of the record format. The device layer in `libraries/EdgeCore/src/flash/w25q32.[ch]` deliberately has no erase operation and requires a caller-approved scratch range for writes.

## Purpose and ownership

Record every processed event in external SPI flash after the application has attempted to queue its MQTT publication, including events with rejected, duplicate or degraded outcomes. Flash logging must not run in `SchedulerTask`, sensor acquisition, or inference. A dedicated, typed `log_q` feeds the lower-priority `ApplicationTask`, which alone owns W25Q32 log operations. The generic SPI HAL remains device-independent; a W25Q32 device layer above it owns commands, device checks, and log integrity.

“Publish first” means **attempt to enqueue** the occupancy result to the existing publisher before offering the log request. It does not assert MQTT broker delivery. A publisher-queue failure and a flash-log failure are independent, observable outcomes; neither may turn a failed reading into a valid one or stop live monitoring.

## On-flash format awaiting D3/D6 agreement

The earlier 32-byte draft cannot represent the current D3 event identity and processing result. It has been retired as an implementation target. After D3/D6 agree the application contract, define an explicit versioned byte layout with a CRC and golden vectors. Serialize fields deliberately; never write a compiler-dependent C/C++ struct image. The record must distinguish an applied occupancy decision from duplicate, reordered, rejected, no-change and degraded processing, and preserve the MQTT **enqueue** result separately from broker delivery. The precise field widths, enum values and CRC convention are still open.

An erased slot, unsupported version, invalid flags/field values, bad CRC, or torn write is **not** a stored event. A record is reported as stored only after program completion and byte-for-byte readback (including CRC) succeed. On reboot, scan the reserved log area, accept only valid records, and report invalid/torn slots without exposing them as occupancy data. Define how scanning skips or stops at a torn slot before coding the log allocator.

**D6 schema dependency:** The Week 6 feedback tracker assigns sensor-node event formation and the canonical event ID (proposed node ID + boot ID + sequence), MQTT de-duplication, and occupancy-after-reset semantics to D6 (F9/F10). Agree how the stored record carries or unambiguously references the D6 ID before programming an event record. D2 logs the agreed event but does not define D6's de-duplication or persistence policy.

Physical slot packing, erase-sector allocation, retention, and wrap policy remain open until the agreed event size and required event rate/retention are known. Never erase an unknown sector or silently wrap and overwrite retained events. The one-time scratch-sector write proves the device layer, not the log allocator. Return a `LOG_FULL` outcome when approved slots are exhausted.

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

- [x] Brendan selected every processed event as the logging scope, including rejected, duplicate and degraded outcomes (25 October 2026).
- [ ] Brendan, Jy and D3/D6 approve queue ownership, record fields/enums/CRC convention, sequence/boot-ID semantics, and MQTT ordering.
- [ ] Confirm the canonical event-ID schema and occupancy persistence boundary with D6 (feedback F9/F10); update the logical record and golden vectors before writing flash.
- [ ] Agree event rate, retention, scratch address, physical slot packing, full/wrap policy, and maximum acceptable sampling delay before enabling erase.
- [ ] Add portable encode/decode and CRC golden-vector tests; test blank, torn, wrong-version, bad-CRC, invalid-field, sequence rollover, and reboot scan cases without a module.
- [ ] Add mocked device tests for absent/wrong JEDEC ID, stuck busy, SPI timeout, partial program, bad readback, full log, and queue overflow. Verify no false “stored” or valid occupancy result.
- [ ] With the physical module: record part marking, 3.3 V check, fixture photo, JEDEC ID, scratch address, write/readback/reboot recovery, and disconnected-device evidence.
- [ ] During live T-23: capture event rate, queue high-water and drops, publisher queue outcome, flash latency/errors, sampling median/p95/p99/max, SPI mutex wait/hold, stack/heap, resets, and watchdog events.

## Review record (fill in before implementation)

| Decision | Agreed value / rationale | Reviewer and date |
|---|---|---|
| Occupancy/class enum and confidence sentinel | TBD | TBD |
| Events to log | Every processed event, including rejected, duplicate and degraded outcomes; exact enum mapping awaits D3/D6. | Brendan, 25 Oct 2026 |
| CRC-32 convention and magic bytes | TBD | TBD |
| Event sequence rollover and boot identifier | TBD | TBD |
| Maximum event rate and minimum retention | TBD | TBD |
| Flash part, JEDEC ID, voltage and maximum approved SPI clock | W25QXX breakout; physical JEDEC `EF4016` at 3.3 V; test clock 1 MHz. Exact top marking and production maximum clock still need confirmation. | Brendan, 25 Oct 2026: physical read/program test only |
| Scratch address, slot layout, erase and full-log policy | Final 4 KiB `0x3FF000–0x3FFFFF` approved as disposable test scratch; 16 bytes at its start programmed once without erase. Event-log slot/retention/erase policy TBD. | Brendan, 25 Oct 2026: one-time test approval only |
| Queue capacity/drop and publication semantics | Provisional: 8, drop newest, enqueue publish first | TBD |
| T-23 latency, queue, stack and heap pass thresholds | TBD | TBD |

See [D2_SPI_DESIGN_NOTES.md](D2_SPI_DESIGN_NOTES.md) for the observed transport/fault findings and [D2_SPI_TODO.md](D2_SPI_TODO.md) for the project acceptance gates.
