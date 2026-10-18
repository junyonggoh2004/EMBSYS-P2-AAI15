#!/usr/bin/env python3
"""Task 12: check the ESP32-S3 HAL on hardware (timing and console).

Drives esp32s3_haltest/esp32s3_haltest.ino over USB serial. It runs the board's HAL
self-test and checks every result, checks that a typed line is echoed back
through hal_console_getc() and printf, and times the board's 1000 ms tick
against the host clock.
"""

import argparse
import re
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports

BAUD = 115200
ESPRESSIF_VID = 0x303A
TICKS = 12   # the first tick is dropped: it can be held up behind the self-test output


def find_port():
    ports = [p.device for p in list_ports.comports() if p.vid == ESPRESSIF_VID]
    return ports[0] if ports else None


def read_until(ser, log, pattern, seconds):
    """Read lines until one matches pattern or the time runs out."""
    lines, buf = [], b""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        buf += ser.read(512)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode(errors="replace").rstrip("\r")
            stamp = time.monotonic()
            log.write(f"{time.strftime('%H:%M:%S')} {line}\n")
            lines.append((stamp, line))
            if pattern and re.search(pattern, line):
                log.flush()
                return lines
    log.flush()
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port (default: the first Espressif USB device)")
    parser.add_argument("--log", default="parity/logs/task12/hal_test.log")
    args = parser.parse_args()

    port = args.port or find_port()
    if not port:
        sys.exit("no ESP32-S3 found; is it plugged in and not held by another terminal?")
    Path(args.log).parent.mkdir(parents=True, exist_ok=True)
    results = []

    with serial.Serial(port, BAUD, timeout=0.1) as ser, open(args.log, "w") as log:
        # wait until the boot-time self-test is over and the loop is ticking
        read_until(ser, log, r"^\[TICK\]", 15)

        ser.write(b"SELFTEST\n")
        lines = read_until(ser, log, r"^\[HALTEST\] done", 15)
        for _, line in lines:
            m = re.match(r"^\[HALTEST\] (PASS|FAIL|SKIP) (\S+) ?(.*)", line)
            if m:
                ok = None if m.group(1) == "SKIP" else m.group(1) == "PASS"
                results.append((m.group(2), ok, m.group(3)))
        done = [line for _, line in lines if line.startswith("[HALTEST] done")]
        results.append(("selftest-complete", bool(done), done[0] if done else "no summary line"))

        text = "hello parity 12345"
        ser.write((text + "\n").encode())
        lines = read_until(ser, log, rf"^\[ECHO\] {text}$", 3)
        echoed = any(line == f"[ECHO] {text}" for _, line in lines)
        results.append(("console-echo", echoed, f"sent '{text}'"))

        lines = read_until(ser, log, None, TICKS + 1.5)
        ticks = [(t, int(m.group(1))) for t, line in lines
                 for m in [re.match(r"^\[TICK\] (\d+)$", line)] if m][1:TICKS]
        if len(ticks) < TICKS - 1:
            results.append(("tick-period", False, f"only {len(ticks)} ticks"))
        else:
            deltas = [b[1] - a[1] for a, b in zip(ticks, ticks[1:])]
            results.append(("tick-period", all(abs(d - 1000) <= 5 for d in deltas),
                            f"board deltas {min(deltas)}..{max(deltas)} ms"))
            board_span = ticks[-1][1] - ticks[0][1]
            host_span = (ticks[-1][0] - ticks[0][0]) * 1000
            # USB delivery jitter is tens of ms; 1% still catches a wrong time base
            results.append(("tick-vs-host-clock", abs(board_span - host_span) <= board_span * 0.01,
                            f"board {board_span} ms, host {host_span:.0f} ms"))

    for name, ok, detail in results:
        label = "SKIP" if ok is None else ("PASS" if ok else "FAIL")
        print(f"{label}  {name:<28} {detail}")
    run = [ok for _, ok, _ in results if ok is not None]
    print(f"\n{sum(run)}/{len(run)} passed, {len(results) - len(run)} skipped. Raw log: {args.log}")
    return 0 if all(run) else 1


if __name__ == "__main__":
    sys.exit(main())
