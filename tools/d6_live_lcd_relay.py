#!/usr/bin/env python3
"""Temporary laptop relay: B's real sensor frames -> C's D6 LCD snapshot.

Occupancy is held at zero until B owns the doorway event counter. The comfort
card is supplied independently by B's live comfort-summer publisher.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


FRAME = re.compile(r"^pico/s3-002/sensor/(?P<source>A_(?:AHT|LIGHT|SONAR_[AB]))/line "
                   r"proto=\S+ src=\S+ len=(?P<length>-?\d+) ts=\d+ : (?P<data>.*)$")


def executable(name):
    found = shutil.which(name)
    if found:
        return found
    path = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "mosquitto" / (name + ".exe")
    if path.is_file():
        return str(path)
    raise FileNotFoundError(path)


def decode(line):
    m = FRAME.match(line.strip())
    if not m or int(m["length"]) < 0:
        return None
    try:
        data = bytes.fromhex(m["data"])
    except ValueError:
        return None
    if len(data) != int(m["length"]):
        return None
    source = m["source"]
    if source == "A_AHT" and len(data) == 6 and not data[0] & 0x80 and data[0] & 0x08:
        humidity = ((data[1] << 12 | data[2] << 4 | data[3] >> 4) * 100 / 1048576)
        temperature = (((data[3] & 15) << 16 | data[4] << 8 | data[5]) * 200 / 1048576 - 50)
        return source, (temperature, humidity)
    if source == "A_LIGHT" and len(data) == 8:
        return source, int.from_bytes(data[4:8], "little")
    if source in ("A_SONAR_A", "A_SONAR_B") and len(data) == 3 and data[2] == 1:
        distance = int.from_bytes(data[:2], "little") * 0.0343 / 2
        if 2 <= distance <= 400:
            return source, distance
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--seconds", type=int, default=0,
                        help="run duration; 0 keeps relaying until Ctrl+C")
    args = parser.parse_args()
    sub = subprocess.Popen([executable("mosquitto_sub"), "-h", args.host,
                            "-t", "pico/s3-002/sensor/+/line", "-v"],
                           stdout=subprocess.PIPE, text=True, bufsize=1)
    values = {}
    end = time.monotonic() + args.seconds if args.seconds else None
    seq = int(time.time())
    last_pub = 0
    print("Live B sensor relay to C started; Ctrl+C to stop. Occupancy is 0 (not counted yet).", flush=True)
    try:
        while end is None or time.monotonic() < end:
            line = sub.stdout.readline()
            if not line:
                break
            sample = decode(line)
            if sample:
                values[sample[0]] = (sample[1], time.monotonic())
            now = time.monotonic()
            needed = ("A_AHT", "A_LIGHT")
            if now - last_pub < 2 or any(k not in values or now - values[k][1] > 6 for k in needed):
                continue
            distances = {}
            for sensor in ("A_SONAR_A", "A_SONAR_B"):
                sample = values.get(sensor)
                distances[sensor] = round(sample[0], 1) if sample and now - sample[1] < 6 else -1
            temperature, humidity = values["A_AHT"][0]
            light = values["A_LIGHT"][0]
            seq += 1
            snapshot = {"v": 3, "seq": seq, "occupancy": 0,
                        "distance_a_cm": distances["A_SONAR_A"],
                        "distance_b_cm": distances["A_SONAR_B"],
                        "temperature_c": round(temperature, 2),
                        "humidity_pct": round(humidity, 2), "light_lux": light,
                        "source": "live"}
            payload = json.dumps(snapshot, separators=(",", ":"))
            result = subprocess.run([executable("mosquitto_pub"), "-h", args.host,
                                     "-q", "1", "-t", "d6/board-b/snapshot", "-m", payload],
                                    capture_output=True, text=True)
            if result.returncode:
                print("Publish failed:", result.stderr.strip(), flush=True)
            else:
                a = distances["A_SONAR_A"]
                b = distances["A_SONAR_B"]
                print(f"seq={seq} A={a if a >= 0 else '--'} cm B={b if b >= 0 else '--'} cm | {temperature:.1f} C | "
                      f"{humidity:.1f}% RH | {light} lux | occupancy pending", flush=True)
                last_pub = now
    except KeyboardInterrupt:
        pass
    finally:
        sub.terminate()
        sub.wait(timeout=2)


if __name__ == "__main__":
    main()
