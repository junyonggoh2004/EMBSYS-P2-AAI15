#!/usr/bin/env python3
"""Read-only physical check of the single synthetic D2 W25Q32 log fixture."""

import argparse
import re
import struct
import sys
import time
import zlib
from pathlib import Path

import serial


def fixture_hex():
    wire = bytearray(80)
    wire[:4] = b"D2LG"
    wire[4:6] = bytes((1, 80))
    struct.pack_into("<Q", wire, 8, 0xD200000000000001)
    struct.pack_into("<I", wire, 16, 0xD2000001)
    struct.pack_into("<I", wire, 20, 1)
    struct.pack_into("<H", wire, 68, 0xFFFF)
    wire[70] = 4  # rejected: this is not a live occupancy event
    wire[73] = 0xFE  # test-only reason marker
    struct.pack_into("<I", wire, 76, zlib.crc32(wire[:76]))
    return wire.hex().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", required=True, type=Path)
    args = parser.parse_args()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    captured = []
    try:
        with args.log.open("w", encoding="utf-8") as log, serial.Serial(
            args.port, 115200, timeout=0.25
        ) as device:
            device.dtr = False
            device.rts = False
            time.sleep(2)
            device.reset_input_buffer()
            log.write("# sent: SPILOGPLAN (read-only)\n")
            device.write(b"SPILOGPLAN\r\n")
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                line = device.readline().decode(errors="replace").strip()
                if not line:
                    continue
                log.write(line + "\n")
                log.flush()
                if "[SPILOG]" in line or "[SPILOGPLAN]" in line:
                    captured.append(line)
                if "[SPILOGPLAN]" in line:
                    break
    except (OSError, serial.SerialException) as exc:
        print(f"FAIL: {exc}; partial log: {args.log}", file=sys.stderr)
        return 1

    for line in captured:
        print(line)
    fixture = next((s for s in captured if "[SPILOG] fixture" in s), "")
    plan = next((s for s in captured if "[SPILOGPLAN]" in s), "")
    match = re.search(r"address=3FF100 bytes=80 hex=([0-9A-F]{160})", fixture)
    passed = (match is not None and match[1] == fixture_hex() and
              "[SPILOGPLAN] BLANK slots=30 valid=0 invalid=0 close=0; no write" in plan)
    print(f"{'PASS' if passed else 'FAIL'}: read-only log plan; log: {args.log}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
