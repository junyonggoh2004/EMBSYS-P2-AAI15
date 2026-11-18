# Board C N16R8 LCD bring-up: 18 November 2026

- Hardware: bare ESP32-S3 N16R8, DIYUSER 76 x 284 ST7789P3 LCD,
  [Board C pin-to-hole wiring](PIN_TO_HOLE_WIRING.md#board-c-tested-lcd-fixture).
- Backed up the board's 16 MB factory flash image locally as
  `tmp/board_c_factory_flash_20261118.bin` in the lcd-output worktree.
  This local backup is not committed.
- Built the D6 firmware with octal PSRAM and `EDGE_NODE_ID=s3-003` in
  `esp32s3_mtk/build-s3-003-n16r8`. Flashed bootloader, partition table,
  and app through the CH343 USB socket on COM12; esptool verified the hashes.
- The native USB socket appeared as COM13. After an RST press, the console
  reported `[BOOT] esp32s3_mtk node s3-003` and started the runtime.
- Sent `LCDTEST 76 284 82 18 00 0` on COM13. Console reported test queued,
  starting with `76x284 offset=82,18 madctl=00 invert=0`, and
  `[LCD] test complete result=0`. The user confirmed red, green, blue and
  black bands plus `LCD READY` on the screen.
- `TASKS` reported `BenchmarkTask stack_free=1756 B`,
  `bench_mbf free=704 B dropped=0`, and `console_rx_dropped=0`.

This passes Board C's **isolated LCD fixture** on the N16R8. It does not
verify live sensor readings, occupancy, B-to-C transport, or stale status.
