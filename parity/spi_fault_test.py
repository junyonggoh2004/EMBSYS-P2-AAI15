#!/usr/bin/env python3
"""D2 connected SPI fault checks on the production firmware and loopback jumper."""

import argparse
import re
import sys
from pathlib import Path

import serial

from spi_test import command, config, read_for


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", default="parity/logs/d2-spi/fault-connected.log")
    args = parser.parse_args()

    log_path = Path(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(name, passed, evidence):
        checks.append((name, passed, evidence))

    with log_path.open("w", encoding="utf-8") as log, serial.Serial(
            args.port, 115200, timeout=0.1) as ser:
        ser.dtr = False
        ser.rts = False
        read_for(ser, 1.0, log)
        command(ser, "STOP", 0.5, log)
        command(ser, "CLEAR", 0.5, log)

        baseline = command(ser, config("BASELINE"), 1.0, log)
        running = command(ser, "RUN", 2.0, log)
        check("baseline-loopback", "CFG: ok" in baseline and
              bool(re.search(r"proto=spi src=BASELINE len=2 ts=\d+ : A5 5A", running)),
              baseline + running)

        # Failed additions must leave the already-running device intact.
        for name, hz in (("clock-too-low", 999), ("clock-too-high", 40000001)):
            response = command(ser, config(name.upper().replace("-", "_"),
                                           **{"spi.hz": str(hz)}), 0.8, log)
            after = read_for(ser, 1.2, log)
            check(name, "spi.hz out of range" in response and
                  "CFG: add failed (-5)" in response and
                  bool(re.search(r"proto=spi src=BASELINE len=2 ts=\d+ : A5 5A", response + after)),
                  response + after)
        command(ser, "STOP", 0.5, log)
        command(ser, "CLEAR", 0.5, log)

        for name, hz in (("clock-min-1khz", 1000), ("clock-max-40mhz", 40000000)):
            sensor = name.upper().replace("-", "_")
            configured = command(ser, config(sensor, **{"spi.hz": str(hz)}), 1.0, log)
            output = command(ser, "RUN", 2.0, log)
            check(name, "CFG: ok" in configured and
                  bool(re.search(rf"proto=spi src={sensor} len=2 ts=\d+ : A5 5A", output)),
                  configured + output)
            command(ser, "STOP", 0.5, log)
            command(ser, "CLEAR", 0.5, log)

        configured = command(ser, config("FORCED_TIMEOUT", **{
            "spi.hz": "1000", "spi.timeout_ms": "1",
            "spi.tx": " ".join(str(i) for i in range(64)),
            "spi.read_len": "128"}), 1.0, log)
        output = command(ser, "RUN", 2.0, log)
        check("transfer-timeout-not-published",
              "CFG: ok" in configured and
              "error[0/FORCED_TIMEOUT]: poll returned -1" in output and
              "proto=spi src=FORCED_TIMEOUT" not in output,
              configured + output)
        command(ser, "STOP", 0.5, log)
        command(ser, "CLEAR", 0.5, log)

    for name, passed, evidence in checks:
        print(f"{'PASS' if passed else 'FAIL'}  {name}")
        if not passed:
            lines = [line for line in evidence.splitlines() if line.strip()]
            print("      " + (lines[-1] if lines else "no serial response"))
    passed = sum(ok for _, ok, _ in checks)
    print(f"\n{passed}/{len(checks)} passed. Raw log: {log_path}")
    return 0 if passed == len(checks) else 1


if __name__ == "__main__":
    sys.exit(main())
