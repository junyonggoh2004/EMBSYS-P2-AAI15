#!/usr/bin/env python3
"""Program the approved synthetic D2 W25Q32 record once, with no retry."""

import argparse
import sys
import time
from pathlib import Path

import serial

from spi_flash_log_plan import fixture_hex
from spi_flash_readonly import response


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", required=True, type=Path)
    args = parser.parse_args()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    try:
        with args.log.open("w", encoding="utf-8") as log, serial.Serial(
            args.port, 115200, timeout=0.25
        ) as device:
            device.dtr = False
            device.rts = False
            time.sleep(2)
            plan = response(device, log, "SPILOGPLAN", "[SPILOGPLAN]", timeout=15)
            raw = args.log.read_text(encoding="utf-8")
            expected = f"[SPILOG] fixture address=3FF100 bytes=80 hex={fixture_hex()}"
            if ("[SPILOGPLAN] BLANK slots=30 valid=0 invalid=0 close=0; no write"
                    not in plan or expected not in raw):
                print(f"FAIL: plan did not match approved bytes and blank area; no write; {args.log}")
                return 1
            print(plan)
            # This is the sole external-flash program command. Never retry it:
            # a timeout may leave a partially programmed slot.
            written = response(device, log, "SPILOGWRITE EF4016 3FF100",
                               "[SPILOGWRITE]", timeout=15)
            print(written)
            if ("[SPILOGWRITE] PASS address=3FF100 bytes=80 " not in written or
                    "readback=verified" not in written):
                print(f"FAIL: write outcome needs inspection; no retry; {args.log}")
                return 1
            verified = response(device, log, "SPILOGVERIFY",
                                "[SPILOGVERIFY]", timeout=15)
            print(verified)
    except (OSError, serial.SerialException, TimeoutError) as exc:
        print(f"FAIL: {exc}; do not retry the write; partial log: {args.log}",
              file=sys.stderr)
        return 1
    passed = "[SPILOGVERIFY] PASS address=3FF100 bytes=80 crc=valid synthetic=1; no write" in verified
    print(f"{'PASS' if passed else 'FAIL'}: one synthetic record; no erase; log: {args.log}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
