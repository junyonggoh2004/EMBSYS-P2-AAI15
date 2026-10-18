#!/usr/bin/env python3
"""NFR6 (task 21): continuous run on the S3 with Wi-Fi and MQTT connected.

Three digital sensors (2, 1 and 0.5 Hz) and a rule with inference run for
--minutes. Passes when the board never resets or panics, every sensor keeps
publishing (no gap over three periods), heartbeats keep arriving on the
broker, and the internal heap at the end is within 4 KB of the start.
"""

import argparse
import re
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt

from net_fault_test import RESET, Node

CFGS = [("E1", 6, 2.0), ("E2", 4, 1.0), ("E3", 7, 0.5)]   # S3 pins
LINE = re.compile(r"^proto=gpio src=(E\d) len=3 ts=(\d+)")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--broker", required=True)
    ap.add_argument("--node", default="s3-001")
    ap.add_argument("--port", required=True)
    ap.add_argument("--minutes", type=float, default=30.0)
    ap.add_argument("--log", default="parity/logs/s3/nfr6-endurance.log")
    args = ap.parse_args()

    beats = []
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="endurance-host")
    client.on_message = lambda c, u, m: beats.append(time.monotonic())
    client.connect(args.broker, 1883)
    client.subscribe(f"pico/{args.node}/heartbeat")
    client.loop_start()

    Path(args.log).parent.mkdir(parents=True, exist_ok=True)
    stamps = {n: [] for n, _, _ in CFGS}
    heaps = []
    with open(args.log, "w") as log:
        node = Node(args.port, log)
        node.send("STOP")
        node.send("CLEAR")
        node.read(2)
        for name, pin, hz in CFGS:
            node.send(f"BEGINCFG|name={name}|proto=gpio|gpio.mode=digital|gpio.pin={pin}"
                      f"|gpio.pull=down|mode=poll|freq_hz={hz:g}|ENDCFG")
            node.read(1)
        node.send(r"BEGINRULE|name=EINF|source=E1|calc=s=u8(0)|when=1"
                  r"|action=batch: infer.set:0:1.0 \| infer.run|ENDRULE")
        node.read(1)
        heaps.append(node.heap())
        node.send("RUN")
        t0 = time.monotonic()
        end = t0 + args.minutes * 60
        next_heap = t0 + 300
        while time.monotonic() < end:
            for _, line in node.read(5):
                m = LINE.match(line)
                if m:
                    stamps[m.group(1)].append(int(m.group(2)))
            if time.monotonic() >= next_heap:
                heaps.append(node.heap())
                next_heap += 300
        node.send("STOP")
        node.read(2)
        heaps.append(node.heap())
        node.send("CLEAR")
        node.read(2)
        resets = [l for _, l in node.lines if RESET.search(l)]

    client.loop_stop()
    results = [("no-reset", not resets, resets[0] if resets else "no reset or panic")]
    for name, _, hz in CFGS:
        ts = stamps[name]
        gap = max((b - a for a, b in zip(ts, ts[1:])), default=None)
        ok = gap is not None and gap <= 3 * 1000 / hz
        results.append((f"samples-{name}", ok, f"{len(ts)} samples, largest gap {gap} ms"))
    bgap = max((b - a for a, b in zip(beats, beats[1:])), default=None)
    results.append(("heartbeats", bgap is not None and bgap < 30,
                    f"{len(beats)} heartbeats, largest gap {bgap:.1f} s" if bgap else "none"))
    ok = None not in heaps and heaps[-1] >= heaps[0] - 4096
    results.append(("heap-stable", ok, "internal heap free every 5 min: " + ", ".join(map(str, heaps))))
    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name:<12} {detail}")
    passed = sum(ok for _, ok, _ in results)
    print(f"\n{passed}/{len(results)} passed after {args.minutes:g} min. Raw log: {args.log}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
