# D6 LCD and sensor-frame integration — 11 November 2026

Implemented on `d6` over source base `035d7fc`, as an uncommitted working
change. LCD component reference: `b1c1675`. Board roles clarified by the user:
**A input only; B ML/storage/data handling/communication; C output**.

## Changes and compatibility

- Selectively imported the ST7789 component, HAL tests, reference photos and
  separate fixture guide. Its earlier panel evidence remains reference-branch
  evidence. Current CMake keeps the existing sensor/ML source registration.
- Added `LCDTEST` to current serial and MQTT platform-command routing. It
  copies a typed request through the nonblocking benchmark buffer; only
  BenchmarkTask calls the driver. Startup performs no LCD initialization.
  Invalid/overflowing arguments and full queues are rejected; SPI and other
  benchmark dispatch remain present. No live display task is introduced.
- Added portable `mqtt/sensor_frame.[ch]` and used it in the existing bridge.
  Topic parsing rejects empty/wildcard/overlong identities and extra segments.
  Envelope parsing checks source, supported protocol, bounded decimal fields,
  exact two-digit hex and declared byte count. Typed consumers can require an
  expected protocol. Parser outputs remain unchanged on failure.
- Preserved the bridge's 64-byte bound and publisher's 255-byte line limit.
  Bare hex and permissive partial-token parsing are intentionally no longer
  accepted. MTK checks original payload length/embedded NULs before using the
  bridge's C-string API. A rejected frame reaches neither bridge publication
  nor rules. Valid zero bytes remain valid data.
- Fixed `mtk.sh check` to retain the actual build failure status. Its former
  pipeline could return success when a failed application build had already
  printed a bootloader size line. A mocked command-runner regression reproduces
  the old failure and covers the corrected success/failure paths.

This is framing and diagnostic integration. Explicit sensor profiles are
still required for physical units; it adds no B→C application payload,
freshness policy, remote ML bindings or storage writes. Source uptime is not
a synchronized cross-board clock.

## Verification actually run

Raw outputs and host/build provenance are retained in
[native-20261111-lcd-parser](../../parity/logs/mtk/d6/native-20261111-lcd-parser/provenance.txt).
These are native/modelled tests and a target build, not physical D6 trials.

| Check | Result and evidence |
|---|---|
| Truncated bridge regression before fix | Reproduced: a declared six-byte frame with two bytes reached publication; rejection assertion failed |
| `sh tools/d6_test.sh` | PASS: frame bounds/topic identity/rejection atomicity, AHT 25 °C / 50% decoding, bridge side effects, LCD HAL test and build-runner regression; [output](../../parity/logs/mtk/d6/native-20261111-lcd-parser/d6-native.log) |
| Same D6 suite with address/undefined-behavior sanitizers | PASS; [output](../../parity/logs/mtk/d6/native-20261111-lcd-parser/d6-sanitizers.log) |
| `sh tools/mtk_runtime_test.sh` | PASS: current application regressions plus LCD serial/MQTT handoff, driver ownership, init failure, queue overflow/drop and remote topic/span rejection; [output](../../parity/logs/mtk/d6/native-20261111-lcd-parser/runtime.log) |
| `sh tools/cody_test.sh` | PASS; [output](../../parity/logs/mtk/d6/native-20261111-lcd-parser/cody.log) |
| `sh tools/d2_host_test.sh` | PASS; [output](../../parity/logs/mtk/d6/native-20261111-lcd-parser/d2-host.log) |
| `sh tools/check_hal_boundary.sh`, `sh tools/mtk.sh purity` | PASS; source purity covers 88 files and shared C boundary; [output](../../parity/logs/mtk/d6/native-20261111-lcd-parser/purity.log) |
| `sh tools/mtk.sh check` | PASS on retry with pinned kernel/SDK/compiler; initial sandbox process-inspection failure and later transient disk-full failure did not count as success; [build summary](../../parity/logs/mtk/d6/native-20261111-lcd-parser/target-build.log) |
| `sh tools/mtk.sh image` | PASS on the newly linked ELF; [output](../../parity/logs/mtk/d6/native-20261111-lcd-parser/target-image.log) |

The runtime suite was rerun after tightening LCD numeric validation. The D6
sanitizer run includes the final parser and build-runner regression. Existing
comment/switch warnings in retained baseline code remain visible in logs.

## Target memory review

The linked map confirms `bench_msg_t` is 40 bytes via its typed 704-byte,
16-message buffer: `16 × (4-byte header + 40-byte aligned message)`. The prior
28-byte message used 512 bytes, so this buffer grows by **192 internal bytes**.
The map places it at `0x3fcc049c`.

ApplicationTask retains a **4096-byte internal stack** at `0x3fcc8d90`, core 1
and priority 23. BenchmarkTask retains its **3072-byte internal stack** at
`0x3fcc8190`. These addresses describe this linked build, not runtime residency
or measured headroom. No stack increase or new task is added.

The imported LCD glyph scratch is **864 bytes of internal BSS** at
`0x3fcd9090`, plus small driver state. New parser code adds no global buffers
or allocation. It replaces the bridge's old local hex scratch with a bounded
frame object. The application image is `0x13f260` bytes, with 58% of the app
partition free. Runtime heap/stack margin under combined load is still unmeasured.

## Remaining acceptance gates

1. Confirm A input inventory/pins, B handling/storage and B→C topic/payload,
   C panel/output fixture, freshness and failure behavior in the worksheet.
2. Run the LCD pattern on the intended isolated C fixture and this exact
   firmware; record visible edges/colors/text, TASKS, stack/heap and drops.
3. Implement typed B handling/forwarding and C output state/handoff. Prove
   T02 A temperature/humidity → B → C, then malformed/stale/recovery trials.
4. Add B's ML after D4 remote bindings/result transport; add storage only after
   its record/writer/failure contract and dedicated trials are agreed.
5. Run D1 `4a`, `5-neg`, MQTT and relevant application/combined-load checks on
   their correct fixtures. No board was flashed and no external flash was written
   during this integration. D2 logging/T-23 and D3 hardware gates remain open.
