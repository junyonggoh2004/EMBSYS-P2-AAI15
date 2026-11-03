#!/usr/bin/env python3
"""Run the D2 SPI HAL mutex timeout check on esp32s3_spilocktest."""

import argparse
import re
import sys
import time
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", default="parity/logs/d2-spi/lock-timeout.log")
    args = parser.parse_args()
    path = Path(args.log)
    path.parent.mkdir(parents=True, exist_ok=True)

    with serial.Serial(args.port, 115200, timeout=0.5) as device, path.open(
            "w", encoding="utf-8") as log:
        device.dtr = False
        device.rts = False
        ready_deadline = time.monotonic() + 10
        while time.monotonic() < ready_deadline:
            device.write(b"PING\r\n")
            line = device.readline().decode(errors="replace").strip()
            if line:
                log.write(line + "\n")
            if line.startswith("[SPILOCK] READY"):
                break
        else:
            print("No SPILOCK READY response", file=sys.stderr)
            return 1
        device.write(b"LOCKTEST\r\n")
        deadline = time.monotonic() + 10
        result = None
        while time.monotonic() < deadline:
            line = device.readline().decode(errors="replace").strip()
            if line:
                log.write(line + "\n")
            if line.startswith("[SPILOCK] ERROR"):
                print(line, file=sys.stderr)
                return 1
            if line.startswith("[SPILOCK] RESULT"):
                result = line
                break
    if result is None:
        print(f"No completed lock test; inspect {path}", file=sys.stderr)
        return 1
    values = {key: int(value) for key, value in re.findall(r"([a-z_]+)=(-?\d+)", result)}
    passed = values.get("started") == 1 and values.get("finished") == 1 and \
        values.get("worker") == 1024 and values.get("contender") == -1 and \
        values.get("close") == 0
    print(f"{'PASS' if passed else 'FAIL'}  {result}")
    print(f"Raw log: {path}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
