#!/usr/bin/env python3
"""NFR1 (task 14): sampling interval and jitter, from the board's timestamps.

Six digital sensors run together at 10, 50, 100, 500, 1000 and 2000 ms; no
wiring is needed (inputs with pull-down). The 100, 500, 1000 and 2000 ms
sensors pass when the mean interval is within 5% of the target over at least
100 samples; 10 and 50 ms are measured and reported with their jitter, as the
Week 6 review asks. Run it with the board's Wi-Fi and MQTT connected.
"""

import argparse
import re
import statistics
import sys
import time
from pathlib import Path

import serial

from parity_test import BAUD, BOARD_PINS, find_port, map_pins, read_for

SENSORS = [("T10", 100.0), ("T50", 20.0), ("T100", 10.0), ("T500", 2.0), ("T1000", 1.0), ("T2000", 0.5)]
PINS = [16, 17, 18, 20, 21, 22]   # Pico numbering, mapped with --board
LINE = re.compile(r"^proto=gpio src=(T\d+) len=3 ts=(\d+) :")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port (default: first usbmodem/ACM port found)")
    parser.add_argument("--board", default="pico", choices=sorted(BOARD_PINS))
    parser.add_argument("--seconds", type=float, default=215.0,
                        help="run time; 215 s gives the 2000 ms sensor 100 intervals")
    parser.add_argument("--log", default="parity/logs/nfr1-timing.log")
    args = parser.parse_args()

    port = args.port or find_port()
    if not port:
        sys.exit("no serial port found")
    pins = BOARD_PINS[args.board]
    Path(args.log).parent.mkdir(parents=True, exist_ok=True)
    stamps = {name: [] for name, _ in SENSORS}

    with serial.Serial(port, BAUD, timeout=0.1) as ser, open(args.log, "w") as log:
        def send(cmd, wait=1.0):
            ser.write((cmd + "\r\n").encode())
            return read_for(ser, wait, log)

        send("STOP")
        send("CLEAR")
        for (name, hz), pin in zip(SENSORS, PINS):
            send(map_pins(f"BEGINCFG|name={name}|proto=gpio|gpio.mode=digital|gpio.pin={pin}"
                          f"|gpio.pull=down|mode=poll|freq_hz={hz:g}|ENDCFG", pins))
        ser.write(b"RUN\r\n")
        end = time.monotonic() + args.seconds
        while time.monotonic() < end:
            for line in read_for(ser, 1.0, log):
                m = LINE.match(line)
                if m:
                    stamps[m.group(1)].append(int(m.group(2)))
        send("STOP", 1.5)
        send("CLEAR", 1.5)

    ok_all = True
    print(f"{'sensor':<7} {'target':>7} {'n':>5} {'mean ms':>9} {'min':>6} {'max':>6} {'stdev':>7}  result")
    for name, hz in SENSORS:
        target = 1000.0 / hz
        ts = stamps[name]
        iv = [b - a for a, b in zip(ts, ts[1:])]
        if len(iv) < 2:
            print(f"{name:<7} {target:7.0f} {len(iv):5}   no data                          FAIL")
            ok_all = False
            continue
        mean, sd = statistics.mean(iv), statistics.pstdev(iv)
        graded = target >= 100
        ok = len(iv) >= 100 and abs(mean - target) <= 0.05 * target
        verdict = ("PASS" if ok else "FAIL") if graded else "measured"
        ok_all = ok_all and (ok or not graded)
        print(f"{name:<7} {target:7.0f} {len(iv):5} {mean:9.2f} {min(iv):6} {max(iv):6} {sd:7.2f}  {verdict}")
    print(f"\nRaw log: {args.log}")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
