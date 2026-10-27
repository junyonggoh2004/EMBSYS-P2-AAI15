#!/usr/bin/env python3
"""Capture physical W25Q32 ID, repeat-read, and pre-program blank-sector evidence.

The three console commands sent here are read-only. They do not program or
erase the external flash. Run only after checking the six-wire 3.3 V fixture;
the blank-sector pass condition is meant for an unused scratch sector.
"""

import argparse
import re
import sys
import time
from pathlib import Path

import serial


def response(device, log, command, prefix, timeout=8):
    device.reset_input_buffer()
    log.write(f"# sent: {command}\n")
    device.write((command + "\r\n").encode("ascii"))
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = device.readline().decode(errors="replace").strip()
        if not line:
            continue
        log.write(line + "\n")
        log.flush()
        if prefix in line:
            return line[line.index(prefix):]
    raise TimeoutError(f"no {prefix} response to {command}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--expected-id", required=True, help="six hex digits observed from the chip")
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    expected = args.expected_id.upper()
    if not re.fullmatch(r"[0-9A-F]{6}", expected) or expected in ("000000", "FFFFFF"):
        parser.error("--expected-id must be six nontrivial hex digits")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    try:
        with args.log.open("w", encoding="utf-8") as log, serial.Serial(
            args.port, 115200, timeout=0.25
        ) as device:
            device.dtr = False
            device.rts = False
            time.sleep(2)
            ident = response(device, log, "SPIID", "[SPIID]")
            check = response(device, log, f"SPICHECK {expected}", "[SPICHECK]")
            scan = response(device, log, f"SPISCAN {expected}", "[SPISCAN]")
    except (OSError, serial.SerialException, TimeoutError) as exc:
        print(f"FAIL: {exc}; partial log: {args.log}", file=sys.stderr)
        return 1
    passed = (f"raw={expected}" in ident and "[SPICHECK] PASS read-only" in check
              and f"id={expected}" in check and "[SPISCAN] BLANK" in scan)
    for line in (ident, check, scan):
        print(line)
    print(f"{'PASS' if passed else 'FAIL'}: read-only physical flash checks; raw log: {args.log}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
