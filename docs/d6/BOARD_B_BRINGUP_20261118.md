# Board B N16R8 physical bring-up — 18 November 2026

This records the first Board B trial. It is not a storage acceptance result.
The [Board B pin-to-hole map](PIN_TO_HOLE_WIRING.md#board-b-external-spi-flash)
is the wiring reference; the SPI flash is a separate external device from
the ESP32-S3's own 16 MB boot flash.

| Check | Observed result |
|---|---|
| ESP32 identity | ESP32-S3 rev 0.2, 16 MB internal flash, 8 MB embedded PSRAM |
| Initial USB port | COM10 CH343 bridge; factory firmware printed RGB demo text |
| Preservation | Full 16,777,216-byte factory flash image read before replacing firmware; saved locally outside the D6 worktree as `tmp/board_b_factory_flash_20261118.bin` in the `lcd-output` worktree |
| D6 build | Separate `build-s3-002-n16r8` image, `EDGE_NODE_ID=s3-002`, octal PSRAM; compiled successfully |
| D6 flash | Bootloader, partition table and app flashed through COM10; esptool verified all written hashes |
| Command console | Native ESP32 USB-Serial-JTAG appeared as COM11 after moving the cable; CH343 did not accept `HELP` |
| External flash ID | Read-only `SPIID` returned `INVALID raw=000000` twice, and again after reseating all six flash connections |
| ESP SPI loopback | With flash DI/DO removed and B12 bridged to B13, the configured SPI poll returned `A5 5A` on 11 consecutive samples; the temporary configuration was stopped and cleared |
| After loopback | Bridge removed and flash DI/DO restored; read-only `SPIID` still returned `INVALID raw=000000` |
| Slower read | A separate read-only 100 kHz, SPI mode 0 JEDEC transfer returned `00 00 00 00` on seven consecutive polls; temporary config stopped and cleared |
| Corrected wiring | After the team applied the updated Board B pin connections, read-only `SPIID` returned `raw=EF4016` |
| Repeated read | `SPICHECK EF4016` passed: 16 bytes at address `000000`, all `FF` |
| Last-sector scan | `SPISCAN EF4016` reported `NONBLANK sector=3FF000 bytes=4096 non_ff=94 first=3FF000`; no write |

The loopback confirmed GPIO12 MOSI and GPIO13 MISO through A12/A13.
The earlier zero JEDEC responses were failed attempts; after the wiring
correction, the external device returned `EF4016` and passed a repeated
read. This verifies the N16R8-to-external-flash read path on the current
fixture. Confirm the physical chip marking before any device-specific write.
The last sector is already nonblank, so it must not be treated as an empty
scratch area.

**No external SPI flash program, erase, or log write was issued.** D6's
occupancy writer, persistent record contract, and A-to-B event path are
still pending. This read-only pass establishes the SPI fixture, not
persistent D6 logging. Do not run `SPIPROGRAM` or `SPILOGWRITE` without an
agreed scratch area and record policy.
