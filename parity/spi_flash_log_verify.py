#!/usr/bin/env python3
"""Read-only recovery check for the approved synthetic D2 W25Q32 record."""

import argparse
import sys
from pathlib import Path

import serial

from spi_flash_program_once import open_console
from spi_flash_readonly import response


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", required=True, type=Path)
    args = parser.parse_args()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    try:
        with args.log.open("w", encoding="utf-8") as log, open_console(args.port) as device:
            result = response(device, log, "SPILOGVERIFY", "[SPILOGVERIFY]", timeout=15)
    except (OSError, serial.SerialException, TimeoutError) as exc:
        print(f"FAIL: {exc}; partial log: {args.log}", file=sys.stderr)
        return 1
    passed = "[SPILOGVERIFY] PASS address=3FF100 bytes=80 crc=valid synthetic=1; no write" in result
    print(result)
    print(f"{'PASS' if passed else 'FAIL'}: read-only cold recovery; log: {args.log}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
