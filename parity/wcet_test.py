#!/usr/bin/env python3
"""SchedulerTask cycle time under load (D5, prof feedback on WCET).

Each scenario clears the node, adds its sensors, runs for --seconds, and reads
the cycle statistics that TASKS prints: the time SchedulerTask spends on one
wake-up (messages, sensor reads, rules, model and inference polling). The
maximum is the measured worst case, not a proven bound. The pulse scenario
needs nothing on GPIO4/GPIO7: with no echo the driver waits for its 25 ms
timeout, which is the blocking cost the feedback asks about.
"""

import argparse
import re
import sys
import time
from pathlib import Path

import serial

from parity_test import BAUD, find_port, read_for

CYCLE = re.compile(r"SchedulerTask cycles=(\d+) mean_us=(\d+) p95_us=(\S+) max_us=(\d+)")

DIG = "BEGINCFG|name={n}|proto=gpio|gpio.mode=digital|gpio.pin={p}|gpio.pull=down|mode=poll|freq_hz=20|ENDCFG"
SCENARIOS = [
    ("idle, sampling on, no sensors", []),
    ("8 digital inputs at 50 ms", [DIG.format(n=f"D{i}", p=p) for i, p in enumerate([1, 2, 4, 5, 6, 8, 9, 10])]),
    ("1 digital input + rule at 50 ms", [DIG.format(n="D0", p=6),
        "BEGINRULE|name=R|source=D0|calc=s=u8(0)|when=s==0|action=log:low|ENDRULE"]),
    ("1 HC-SR04 pulse read at 50 ms, no echo (timeout 25 ms)",
     ["BEGINCFG|name=SON|proto=gpio|gpio.mode=pulse|gpio.trig=4|gpio.echo=7|gpio.pull=down"
      "|gpio.pulse_timeout_us=25000|mode=poll|freq_hz=20|ENDCFG"]),
    ("2 HC-SR04 pulse reads at 50 ms, no echo",
     ["BEGINCFG|name=SONA|proto=gpio|gpio.mode=pulse|gpio.trig=4|gpio.echo=7|gpio.pull=down"
      "|gpio.pulse_timeout_us=25000|mode=poll|freq_hz=20|ENDCFG",
      "BEGINCFG|name=SONB|proto=gpio|gpio.mode=pulse|gpio.trig=8|gpio.echo=9|gpio.pull=down"
      "|gpio.pulse_timeout_us=25000|mode=poll|freq_hz=20|ENDCFG"]),
    ("inference rule at 50 ms", [DIG.format(n="D0", p=6),
        r"BEGINRULE|name=RI|source=D0|calc=s=u8(0)|when=1|action=batch: infer.set:0:1.0 \| infer.run|ENDRULE"]),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", help="serial port (default: first ESP32-S3 found)")
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--log", default="parity/logs/s3/wcet.log")
    args = ap.parse_args()
    port = args.port or find_port()
    if not port:
        sys.exit("no serial port found")
    Path(args.log).parent.mkdir(parents=True, exist_ok=True)

    rows = []
    with serial.Serial(port, BAUD, timeout=0.1) as ser, open(args.log, "w") as log:
        def send(cmd, wait=1.0):
            ser.write((cmd + "\r\n").encode())
            return read_for(ser, wait, log)

        for name, cfgs in SCENARIOS:
            log.write(f"===== {name}\n")
            send("STOP"); send("CLEAR")
            for c in cfgs:
                send(c)
            send("TASKS", 1.5)                      # resets the statistics
            send("RUN")
            read_for(ser, args.seconds, log)
            send("STOP")
            stats = None
            for line in send("TASKS", 1.5):
                m = CYCLE.search(line)
                if m:
                    stats = m.groups()
            rows.append((name, stats))
        send("CLEAR")

    print(f"{'scenario':<55} {'cycles':>7} {'mean us':>8} {'p95 us':>9} {'max us':>8}")
    for name, st in rows:
        if st:
            print(f"{name:<55} {st[0]:>7} {st[1]:>8} {st[2]:>9} {st[3]:>8}")
        else:
            print(f"{name:<55}  no statistics (old firmware? flash first)")
    print(f"\nRaw log: {args.log}")
    return 0 if all(st for _, st in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
