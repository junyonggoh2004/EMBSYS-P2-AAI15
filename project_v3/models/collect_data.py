#!/usr/bin/env python3
"""
collect_data.py — Pico Sensor Data Collector for Model Retraining
==================================================================

Subscribes to Pico data collection MQTT topics and writes incoming
feature vectors to a CSV file. Use this to capture labeled training
data from your live sensor configuration.

Usage:
  # Collect 60 seconds of NORMAL accelerometer data:
  python collect_data.py --broker 192.168.50.192 --tag accel --label 0 --duration 60 --out dataset.csv

  # Collect 30 seconds of ANOMALY data (e.g. shake the sensor), appending:
  python collect_data.py --broker 192.168.50.192 --tag accel --label 1 --duration 30 --out dataset.csv --append

  # Collect from multiple tags simultaneously, no label (unsupervised):
  python collect_data.py --broker 192.168.50.192 --tag accel --tag h206 --out dataset.csv

Requirements:
  pip install paho-mqtt
"""

import argparse
import csv
import json
import sys
import time
import threading
from datetime import datetime

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("ERROR: paho-mqtt not found. Run: pip install paho-mqtt")
    sys.exit(1)


# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------
collected_rows = []
fieldnames_seen = set()
stop_event = threading.Event()
start_time = time.time()


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        tags = userdata["tags"]
        node = userdata["node"]
        for tag in tags:
            topic = f"pico/{node}/data/{tag}"
            client.subscribe(topic, qos=0)
            print(f"[INFO] Subscribed to: {topic}")
    else:
        print(f"[ERROR] Connection failed with code {rc}")


def on_message(client, userdata, msg):
    global fieldnames_seen

    try:
        payload = json.loads(msg.payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        print(f"[WARN] Skipping bad payload: {e}")
        return

    # Inject metadata
    payload["_recv_time"] = datetime.now().isoformat(timespec="milliseconds")
    payload["_topic"] = msg.topic.split("/")[-1]  # The tag name
    if userdata["label"] is not None:
        payload["label"] = userdata["label"]

    # Track all unique keys we've seen (for CSV headers)
    fieldnames_seen.update(payload.keys())
    collected_rows.append(payload)

    ts = payload.get("ts", "?")
    source = payload.get("source", "?")
    count = len(collected_rows)
    print(f"[DATA] #{count:4d}  ts={ts}  source={source}  {msg.topic.split('/')[-1]}")


def write_csv(out_path, append, rows, fieldnames):
    # Canonical column order: metadata first, then data fields, then label
    meta = ["_recv_time", "_topic", "ts", "source"]
    data_cols = sorted(f for f in fieldnames if f not in meta and f != "label")
    label_col = ["label"] if "label" in fieldnames else []
    ordered = meta + data_cols + label_col

    mode = "a" if append else "w"
    newfile = not append or not __import__("os").path.exists(out_path)

    with open(out_path, mode, newline="") as f:
        writer = csv.DictWriter(f, fieldnames=ordered, extrasaction="ignore")
        if newfile:
            writer.writeheader()
        for row in rows:
            writer.writerow(row)

    print(f"\n[INFO] Wrote {len(rows)} rows to '{out_path}'")


def main():
    parser = argparse.ArgumentParser(
        description="Collect Pico sensor features from MQTT into a CSV file."
    )
    parser.add_argument("--broker", default="localhost",
                        help="MQTT broker IP/hostname (default: localhost)")
    parser.add_argument("--port", type=int, default=1883,
                        help="MQTT broker port (default: 1883)")
    parser.add_argument("--node", default="pico-001",
                        help="Pico node ID (default: pico-001)")
    parser.add_argument("--tag", action="append", dest="tags",
                        metavar="TAG", default=[],
                        help="Data tag to subscribe to (can repeat for multiple tags)")
    parser.add_argument("--label", type=float, default=None,
                        help="Label to inject into every row (e.g. 0=normal, 1=anomaly)")
    parser.add_argument("--duration", type=float, default=None,
                        help="Auto-stop after this many seconds (default: run until Ctrl+C)")
    parser.add_argument("--out", default="dataset.csv",
                        help="Output CSV file (default: dataset.csv)")
    parser.add_argument("--append", action="store_true",
                        help="Append to existing CSV instead of overwriting")

    args = parser.parse_args()

    if not args.tags:
        args.tags = ["collect"]  # default tag from rule_action.c

    print("=" * 60)
    print(f"  Pico Data Collector")
    print(f"  Broker  : {args.broker}:{args.port}")
    print(f"  Node    : {args.node}")
    print(f"  Tags    : {args.tags}")
    print(f"  Label   : {args.label}")
    print(f"  Duration: {args.duration or 'until Ctrl+C'} s")
    print(f"  Output  : {args.out} ({'append' if args.append else 'overwrite'})")
    print("=" * 60)

    userdata = {
        "tags": args.tags,
        "node": args.node,
        "label": args.label,
    }

    client = mqtt.Client(userdata=userdata)
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(args.broker, args.port, keepalive=60)
    except Exception as e:
        print(f"[ERROR] Cannot connect to broker: {e}")
        sys.exit(1)

    client.loop_start()
    print("[INFO] Collecting... Press Ctrl+C to stop early.\n")

    try:
        while True:
            time.sleep(0.1)
            if args.duration and (time.time() - start_time) >= args.duration:
                print("[INFO] Duration reached, stopping.")
                break
    except KeyboardInterrupt:
        print("\n[INFO] Interrupted by user.")

    client.loop_stop()
    client.disconnect()

    if collected_rows:
        write_csv(args.out, args.append, collected_rows, fieldnames_seen)
    else:
        print("[WARN] No data was collected. Check that:")
        print("       1. The Pico is running and connected to MQTT.")
        print("       2. Your rule action includes 'record.json:<tag>:<vars>'")
        print("       3. The tag name matches --tag argument.")


if __name__ == "__main__":
    main()
