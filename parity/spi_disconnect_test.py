#!/usr/bin/env python3
"""Observe the production SPI receive path with the MOSI-to-MISO jumper removed."""

import argparse
import re
import sys
from pathlib import Path

import serial

from spi_test import command, config, read_for


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", default="parity/logs/d2-spi/disconnected.log")
    args = parser.parse_args()
    log_path = Path(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)

    expected = list(range(64))
    tx = " ".join(str(value) for value in expected)
    with log_path.open("w", encoding="utf-8") as log, serial.Serial(
            args.port, 115200, timeout=0.1) as ser:
        ser.dtr = False
        ser.rts = False
        read_for(ser, 1.0, log)
        command(ser, "STOP", 0.5, log)
        command(ser, "CLEAR", 0.5, log)
        configured = command(ser, config("DISCONNECTED", **{
            "spi.tx": tx, "spi.read_len": "64", "freq_hz": "2"}), 1.0, log)
        output = command(ser, "RUN", 3.0, log)
        command(ser, "STOP", 0.5, log)
        command(ser, "CLEAR", 0.5, log)

    found = re.findall(
        r"proto=spi src=DISCONNECTED len=64 ts=\d+ : ((?:[0-9A-F]{2} ?){64})",
        output,
    )
    actual = [[int(value, 16) for value in sample.split()] for sample in found]
    mismatched = [sum(a != b for a, b in zip(sample, expected)) for sample in actual]
    print(f"Configuration accepted: {'yes' if 'CFG: ok' in configured else 'no'}")
    print(f"Published samples while jumper was removed: {len(actual)}")
    print(f"Mismatched bytes per sample: {mismatched}")
    print(f"Raw log: {log_path}")
    if not actual:
        print("No published sample observed; inspect the raw log.")
        return 1
    if all(count == 0 for count in mismatched):
        print("Receive data still matched TX; verify that GPIO12-to-GPIO13 jumper was removed.")
        return 1
    print("A disconnected MISO produced mismatched bytes. The host detected them; "
          "the generic production SPI driver published the raw bytes as a sample.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
