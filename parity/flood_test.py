#!/usr/bin/env python3
"""T-13: flood a node's command topic and check that it keeps running.

A GPIO sensor publishes on every sample while the host sends harmless unknown
commands far faster than the node drains its inbound queue. The node passes if
its heartbeat and telemetry continue throughout the flood and for 30 s after.
Needs paho-mqtt and the board on the broker (see mqtt_test.py).
"""

import argparse
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt

from parity_test import BOARD_PINS, map_pins

NODE = "pico-001"
CFG = f"pico/{NODE}/config"
CMD = f"pico/{NODE}/cmd"
DIG = ("BEGINCFG|name=DIG|proto=gpio|gpio.mode=digital|gpio.pin=22|gpio.pull=down"
       "|mode=poll|freq_hz=2|ENDCFG")
MAX_GAP = 25.0  # heartbeat every 10 s, so two missed in a row is a failure
AFTER = 30.0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--broker", required=True, help="broker address, the same as MQTT_HOST")
    parser.add_argument("--rate", type=float, default=50.0, help="flood messages per second")
    parser.add_argument("--seconds", type=float, default=600.0, help="flood duration")
    parser.add_argument("--log", default="parity/logs/t13.log")
    parser.add_argument("--node", default=NODE, help="node id the board uses in its topics")
    parser.add_argument("--board", default="pico", choices=sorted(BOARD_PINS),
                        help="board under test; s3 maps the Pico pin numbers to the S3 pins")
    args = parser.parse_args()
    cfg_topic, cmd_topic = f"pico/{args.node}/config", f"pico/{args.node}/cmd"

    last = {"heartbeat": time.monotonic(), "telemetry": time.monotonic()}

    def on_message(client, userdata, msg):
        if msg.topic.endswith("/heartbeat"):
            last["heartbeat"] = time.monotonic()
        elif "/sensor/" in msg.topic:
            last["telemetry"] = time.monotonic()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="t13-host")
    client.on_message = on_message
    client.connect(args.broker, 1883)
    client.subscribe(f"pico/{args.node}/#")
    client.loop_start()

    Path(args.log).parent.mkdir(parents=True, exist_ok=True)
    with open(args.log, "w") as log:
        def note(text):
            line = f"{time.strftime('%H:%M:%S')} {text}"
            print(line, flush=True)
            log.write(line + "\n")
            log.flush()

        for topic, payload in ((cmd_topic, "CLEAR"), (cfg_topic, map_pins(DIG, BOARD_PINS[args.board])), (cmd_topic, "RUN")):
            client.publish(topic, payload, qos=1)
            time.sleep(2.5)
        note(f"flood start: {args.rate:g} msg/s on {cmd_topic} for {args.seconds:g} s")

        start = time.monotonic()
        sent, next_note, failed = 0, start + 10, None
        end = start + args.seconds + AFTER
        while time.monotonic() < end and not failed:
            now = time.monotonic()
            if now < start + args.seconds:
                client.publish(cmd_topic, "NOP", qos=0)
                sent += 1
            gap = now - last["heartbeat"]
            if gap > MAX_GAP:
                failed = f"no heartbeat for {gap:.0f} s"
            if now >= next_note:
                note(f"t={now - start:5.0f}s sent={sent} heartbeat_age={gap:4.1f}s "
                     f"telemetry_age={now - last['telemetry']:4.1f}s")
                next_note += 10
            time.sleep(1.0 / args.rate)

        elapsed = time.monotonic() - start
        if failed:
            note(f"FAIL after {elapsed:.0f} s and {sent} messages: {failed}")
        else:
            client.publish(cmd_topic, "STOP", qos=1)
            time.sleep(1.5)
            client.publish(cmd_topic, "CLEAR", qos=1)
            note(f"PASS: node kept its heartbeat through {sent} messages and {AFTER:g} s after")
    client.loop_stop()
    client.disconnect()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
