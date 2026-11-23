#!/usr/bin/env python3
"""Show decoded D6 sensor measurements from the existing MQTT line topics.

Uses the same frame layouts and validity checks as EdgeCore's
sensor_measurement.c. No new board firmware or MQTT topic is required.
"""

import argparse
from datetime import datetime
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Optional


FRAME = re.compile(
    r"^pico/(?P<node>[^/]+)/sensor/(?P<source>[^/]+)/line\s+"
    r"proto=\S+\s+src=\S+\s+len=(?P<length>-?\d+)\s+"
    r"ts=(?P<timestamp>\d+)\s*:\s*(?P<payload>.*)$"
)


def decode_line(line: str) -> Optional[str]:
    """Return one readable measurement, or None for a non-sensor message."""
    match = FRAME.match(line.strip())
    if not match:
        return None
    source = match["source"]
    sensor = source[2:] if source.startswith("A_") else source
    label = f"{match['node']} {source}"
    length = int(match["length"])
    if length < 0:
        return f"{label}: sensor error {length}"
    try:
        data = bytes.fromhex(match["payload"])
    except ValueError:
        return f"{label}: invalid hex frame"
    if len(data) != length:
        return f"{label}: invalid frame length"

    if sensor == "AHT":
        if length != 6 or data[0] & 0x80 or not data[0] & 0x08:
            return f"{label}: invalid AHT reading"
        rh_raw = data[1] << 12 | data[2] << 4 | data[3] >> 4
        temp_raw = (data[3] & 0x0F) << 16 | data[4] << 8 | data[5]
        return (
            f"{label}: {temp_raw * 200 / 1048576 - 50:.2f} C, "
            f"{rh_raw * 100 / 1048576:.2f} %RH"
        )
    if sensor == "LIGHT":
        if length != 8:
            return f"{label}: invalid light reading"
        return f"{label}: {int.from_bytes(data[4:8], 'little')} lux"
    if sensor in ("SONAR_A", "SONAR_B"):
        if length != 3 or data[2] != 1:
            return f"{label}: invalid echo reading"
        echo_us = int.from_bytes(data[:2], "little")
        distance_cm = echo_us * 0.0343 / 2
        if not 2 <= distance_cm <= 400:
            return f"{label}: invalid distance ({echo_us} us echo)"
        return f"{label}: {distance_cm:.1f} cm ({echo_us} us echo)"
    return f"{label}: unsupported sensor profile"


def subscriber_path() -> str:
    found = shutil.which("mosquitto_sub")
    if found:
        return found
    windows = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "mosquitto" / "mosquitto_sub.exe"
    if windows.is_file():
        return str(windows)
    raise FileNotFoundError("mosquitto_sub was not found; install Mosquitto or add it to PATH")


def main() -> int:
    parser = argparse.ArgumentParser(description="Print live AHT, light and sonar values from D6 MQTT frames")
    parser.add_argument("--board", choices=("A", "B"), default="A", help="A's local sensors or B's relayed copies")
    parser.add_argument("--host", default="127.0.0.1", help="broker address from this laptop (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--count", type=int, default=0, help="stop after this many MQTT frames; default: continuous")
    parser.add_argument("--wait", type=int, default=0, help="timeout in seconds; default: no timeout")
    args = parser.parse_args()
    node = "s3-001" if args.board == "A" else "s3-002"
    cmd = [subscriber_path(), "-h", args.host, "-p", str(args.port), "-t", f"pico/{node}/sensor/+/line", "-v"]
    if args.count > 0:
        cmd.extend(("-C", str(args.count)))
    if args.wait > 0:
        cmd.extend(("-W", str(args.wait)))
    print(f"Watching Board {args.board} live values via MQTT; Ctrl+C to stop", flush=True)
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True, bufsize=1)
    try:
        assert process.stdout is not None
        for line in process.stdout:
            result = decode_line(line)
            if result:
                print(f"{datetime.now():%H:%M:%S} {result}", flush=True)
        return process.wait()
    except KeyboardInterrupt:
        return 0
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    sys.exit(main())
