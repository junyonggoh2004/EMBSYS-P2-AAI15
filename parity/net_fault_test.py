#!/usr/bin/env python3
"""Task 19 fault tests on an ESP32-S3 node (T-11, T-12; T-13 is flood_test.py).

Checks, in order: a malformed JSON configuration and an oversized payload are
refused without a reset; after the broker stops and restarts the node
reconnects within 30 s (NFR5); after a Wi-Fi drop (console command WIFIDROP)
it reconnects within 30 s and keeps sampling while offline (FR21); the free
heap does not shrink and the board never resets. Needs paho-mqtt and, for the
broker check, permission to stop and restart the local Mosquitto.
"""

import argparse
import queue
import re
import subprocess
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt
import serial

from parity_test import BAUD, BOARD_PINS, map_pins

DIG = ("BEGINCFG|name=OFFL|proto=gpio|gpio.mode=digital|gpio.pin=22|gpio.pull=down"
       "|mode=poll|freq_hz=2|ENDCFG")
RESET = re.compile(r"Guru Meditation|^rst:0x|^\[BOOT\]")


class Node:
    def __init__(self, port, log):
        self.ser = serial.Serial(port, BAUD, timeout=0.1)
        self.log = log
        self.buf = b""
        self.lines = []

    def read(self, seconds, until=None):
        """Collect lines for up to seconds; stop early once `until` matches."""
        got, end = [], time.monotonic() + seconds
        while time.monotonic() < end:
            self.buf += self.ser.read(1024)
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                line = raw.decode(errors="replace").rstrip("\r")
                self.log.write(f"{time.strftime('%H:%M:%S')} {line}\n")
                got.append((time.monotonic(), line))
                if until and re.search(until, line):
                    self.lines += got
                    self.log.flush()
                    return got
        self.lines += got
        self.log.flush()
        return got

    def send(self, text):
        self.log.write(f"{time.strftime('%H:%M:%S')} --- console: {text[:80]}\n")
        self.ser.write((text + "\r\n").encode())

    def heap(self):
        self.send("TASKS")
        for _, line in self.read(3, until=r"heap_free="):
            m = re.search(r"heap_free=(\d+)", line)
            if m:
                return int(m.group(1))
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--broker", required=True)
    ap.add_argument("--node", default="s3-001")
    ap.add_argument("--port", required=True)
    ap.add_argument("--board", default="s3", choices=sorted(BOARD_PINS))
    ap.add_argument("--mosquitto-conf", help="restart Mosquitto with this config for the broker-loss check")
    ap.add_argument("--log", default="parity/logs/task19/s3-net-faults.log")
    args = ap.parse_args()

    cfg_t, cmd_t = f"pico/{args.node}/config", f"pico/{args.node}/cmd"
    inbox = queue.Queue()
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="fault-host")
    client.on_message = lambda c, u, m: inbox.put(m.topic)
    client.connect(args.broker, 1883)
    client.subscribe(f"pico/{args.node}/#")
    client.loop_start()

    Path(args.log).parent.mkdir(parents=True, exist_ok=True)
    results = []
    with open(args.log, "w") as log:
        node = Node(args.port, log)
        node.read(1)
        heap0 = node.heap()

        def mqtt_alive(label):
            client.publish(cmd_t, "SHOW", qos=1)
            ok = any("[CMD] SHOW" in l for _, l in node.read(6, until=r"^\[CMD\] SHOW"))
            results.append((label, ok, "node answered SHOW over MQTT" if ok else "no answer"))

        # T-12: malformed JSON and an oversized payload
        client.publish(cfg_t, '{"configs":[{"name":"BAD",', qos=1)
        got = node.read(6, until=r"unrecognized payload|try_convert_to_inline -> -")
        ok = any(re.search(r"unrecognized payload|try_convert_to_inline -> -", l) for _, l in got)
        results.append(("malformed-json-refused", ok, "parser rejected it" if ok else "no rejection seen"))
        mqtt_alive("alive-after-malformed")

        client.publish(cmd_t, "X" * 5000, qos=1)
        # Up to the client buffer the node truncates with a warning; beyond it
        # the MQTT library drops the connection and the node reconnects.
        got = node.read(6, until=r"truncated|disconnected from broker|Connecting to")
        ok = any(re.search(r"truncated|disconnected from broker|Connecting to", l) for _, l in got)
        results.append(("oversized-payload-refused", ok, "payload refused (truncated or connection reset)" if ok else "no sign of refusal"))
        mqtt_alive("alive-after-oversized")

        # T-11: broker loss
        if args.mosquitto_conf:
            subprocess.run(["pkill", "-f", "sbin/mosquitto -c"], check=False)
            got = node.read(40, until=r"disconnected from broker")
            lost = any("disconnected from broker" in l for _, l in got)
            time.sleep(3)
            subprocess.Popen(["/opt/homebrew/sbin/mosquitto", "-c", args.mosquitto_conf, "-v"],
                             stdout=open(Path(args.log).with_suffix(".broker.log"), "w"),
                             stderr=subprocess.STDOUT, start_new_session=True)
            t0 = time.monotonic()
            got = node.read(40, until=r"Subscribed OK to: broadcast")
            back = [t for t, l in got if "Subscribed OK to: broadcast" in l]
            secs = back[0] - t0 if back else None
            results.append(("broker-loss-reconnect", lost and secs is not None and secs <= 30,
                            f"loss seen={lost}, back after {secs:.1f} s" if secs else f"loss seen={lost}, not back"))
            time.sleep(2)
            client.reconnect()
            client.subscribe(f"pico/{args.node}/#")

        # T-11: Wi-Fi loss, with sampling continuing offline (FR21)
        node.send("STOP")
        node.send("CLEAR")
        node.read(2)
        node.send(map_pins(DIG, BOARD_PINS[args.board]))
        node.send("RUN")
        node.read(2)
        node.send("WIFIDROP")
        got = node.read(12, until=r"hold over")
        offline_samples = sum(1 for _, l in got if l.startswith("proto=gpio src=OFFL"))
        results.append(("samples-while-offline", offline_samples >= 10,
                        f"{offline_samples} samples during the 10 s drop"))
        t0 = time.monotonic()
        got = node.read(40, until=r"Subscribed OK to: broadcast")
        back = [t for t, l in got if "Subscribed OK to: broadcast" in l]
        secs = back[0] - t0 if back else None
        results.append(("wifi-loss-reconnect", secs is not None and secs <= 30,
                        f"back on the broker {secs:.1f} s after Wi-Fi returned" if secs else "not back"))
        node.send("STOP")
        node.send("CLEAR")
        node.read(2)

        heap1 = node.heap()
        results.append(("no-heap-loss", heap0 is not None and heap1 is not None and heap1 >= heap0 - 4096,
                        f"heap free {heap0} B before, {heap1} B after"))
        resets = [l for _, l in node.lines if RESET.search(l)]
        results.append(("no-reset", not resets, resets[0] if resets else "no reset or panic in the log"))

    client.loop_stop()
    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name:<28} {detail}")
    passed = sum(ok for _, ok, _ in results)
    print(f"\n{passed}/{len(results)} passed. Raw log: {args.log}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
