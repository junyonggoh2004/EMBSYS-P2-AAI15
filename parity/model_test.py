#!/usr/bin/env python3
"""Task 20 model storage tests on an ESP32-S3 node (T-14, T-15, T-16).

In order: a valid upload with send_model.py is verified, saved and loaded;
a corrupt upload (wrong CRC32), an oversized one and an interrupted one are
refused and the previous model keeps running inference; after a reset the
saved model loads again without a re-upload; arena use is recorded.
Needs paho-mqtt and the local broker.
"""

import argparse
import re
import subprocess
import sys
import time
import zlib
from pathlib import Path

import paho.mqtt.client as mqtt
import serial

from net_fault_test import Node

MODELS = Path(__file__).resolve().parent.parent / "project_v3" / "models"
MODEL = MODELS / "autoencoder_int8 (2).tflite"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--broker", required=True)
    ap.add_argument("--node", default="s3-001")
    ap.add_argument("--port", required=True)
    ap.add_argument("--log", default="parity/logs/s3/model.log")
    args = ap.parse_args()

    base = f"pico/{args.node}/model"
    blob = MODEL.read_bytes()
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="model-host")
    client.connect(args.broker, 1883)
    client.loop_start()

    def pub(topic, payload, qos=1):
        client.publish(topic, payload, qos=qos).wait_for_publish()

    def start(size, crc):
        pub(f"{base}/start", f'{{"size":{size},"crc32":{crc}}}')

    def chunks(data, upto=None):
        for i in range(0, len(data), 1024)[:upto]:
            pub(f"{base}/chunk/{i // 1024}", data[i:i + 1024], qos=0)
            time.sleep(0.03)

    Path(args.log).parent.mkdir(parents=True, exist_ok=True)
    results = []
    with open(args.log, "w") as log:
        node = Node(args.port, log)
        node.read(1)

        def expect(name, seconds, want, refuse=()):
            got = [l for _, l in node.read(seconds)]
            missing = [w for w in want if not any(re.search(w, l) for l in got)]
            bad = [r for r in refuse if any(re.search(r, l) for l in got)]
            results.append((name, not missing and not bad,
                            "ok" if not missing and not bad else f"missing {missing} unexpected {bad}"))

        def infer_works(name):
            pub(f"pico/{args.node}/cmd", "batch: infer.set:0:1.0 | infer.run")
            expect(name, 5, [r"^\[INFER\] invoke=\d+us"])

        # T-14: valid upload, saved and loaded
        subprocess.run([sys.executable, "send_model.py", "--broker", args.broker, "--node", args.node,
                        "--model", MODEL.name, "--norm", "norm_stats (2).json",
                        "--threshold", "thresholds (2).json"], cwd=MODELS, stdout=log, stderr=subprocess.STDOUT)
        expect("valid-upload", 8, [rf"Transfer complete! {len(blob)} bytes verified", r"Auto-load successful"])
        infer_works("infer-after-upload")

        # T-15: corrupt, oversized and interrupted uploads
        start(len(blob), (zlib.crc32(blob) ^ 1) & 0xFFFFFFFF)
        chunks(blob)
        pub(f"{base}/end", b"")
        expect("corrupt-refused", 5, [r"CRC32 mismatch", r"Previous model kept"])
        start(300000, 1)
        expect("oversized-refused", 3, [r"Invalid size 300000", r"Previous model kept"])
        start(len(blob), zlib.crc32(blob))
        chunks(blob, upto=5)
        start(len(blob), zlib.crc32(blob))
        expect("interrupted-restarts", 3, [r"Previous upload interrupted"])
        infer_works("infer-after-bad-uploads")

        # T-14: the saved model survives a reset (the cut upload's temp file is dropped)
        node.ser.dtr = False
        node.ser.rts = True
        time.sleep(0.2)
        node.ser.rts = False
        node.ser.close()
        time.sleep(4)
        node.ser = serial.Serial(args.port, 115200, timeout=0.1)
        # The boot line "Loaded saved model" is printed before the USB port
        # reopens; auto-load only runs when a stored model is ready.
        expect("persists-after-reset", 25, [r"Auto-load successful"],
               refuse=[r"Auto-load FAILED"])
        node.read(10, until=r"Subscribed OK to: broadcast")
        infer_works("infer-after-reset")

        # T-16: arena use, from the loader's report
        arena = [l for _, l in node.lines if "Arena:" in l]
        results.append(("arena-recorded", bool(arena), arena[-1].split("] ", 1)[-1] if arena else "no report"))

    client.loop_stop()
    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name:<26} {detail}")
    passed = sum(ok for _, ok, _ in results)
    print(f"\n{passed}/{len(results)} passed. Raw log: {args.log}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
