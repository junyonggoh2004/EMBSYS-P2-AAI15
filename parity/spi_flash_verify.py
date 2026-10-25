#!/usr/bin/env python3
"""Capture a read-only W25Q32 D2 scratch-pattern verification after power loss."""

import argparse
import sys
from pathlib import Path

import serial

from spi_flash_program_once import EXPECTED, open_console
from spi_flash_readonly import response


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    try:
        with args.log.open("w", encoding="utf-8") as log, open_console(args.port) as device:
            line = response(device, log, f"SPIVERIFY {EXPECTED}", "[SPIVERIFY]")
    except (OSError, serial.SerialException, TimeoutError) as exc:
        print(f"FAIL: {exc}; partial log: {args.log}", file=sys.stderr)
        return 1
    passed = "[SPIVERIFY] PASS address=3FF000 bytes=16 pattern=retained" in line
    print(line)
    print(f"{'PASS' if passed else 'FAIL'}: read-only external-flash verification; log: {args.log}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
