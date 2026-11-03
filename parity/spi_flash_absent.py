#!/usr/bin/env python3
"""Capture physical absent-flash rejection, with all module wires removed."""

import argparse
import re
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
            line = response(device, log, f"SPICHECK {EXPECTED}", "[SPICHECK]")
    except (OSError, serial.SerialException, TimeoutError) as exc:
        print(f"FAIL: {exc}; partial log: {args.log}", file=sys.stderr)
        return 1
    match = re.search(r"\[SPICHECK\] ERROR probe=(-?\d+) read1=(-?\d+) read2=(-?\d+) close=(-?\d+)", line)
    passed = match is not None and int(match[1]) in (-9, -7) and int(match[4]) == 0
    print(line)
    print(f"{'PASS' if passed else 'FAIL'}: missing device rejected; log: {args.log}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
