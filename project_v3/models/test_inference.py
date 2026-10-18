#!/usr/bin/env python3
"""
test_inference.py — Paso (Step-by-Step) Inference Pipeline Tests
=================================================================

Sends known synthetic inputs to the Pico's ML slot system via MQTT
and validates the console output to verify the inference pipeline
is functioning correctly end-to-end without needing physical sensors.

Requires:
  pip install paho-mqtt

Usage:
  # Run all paso tests (default broker):
  python test_inference.py --broker 192.168.50.192

  # Run only a specific test:
  python test_inference.py --broker 192.168.50.192 --test normalised_zero

Tests reference the baked-in normalization from model_params.h / norm_stats (2).json:
  Slot 0 (f0..f3): mean [28.64, 0.0, 28.64, 28.64], std [10.69, 1.0, 10.69, 10.69]
  Slot 1 (f4..f7): mean [79.23, 0.0, 79.23, 79.23], std [29.65, 1.0, 29.65, 29.65]
  Slot 2 (f8..f11): mean [0.597, 0.056, 0.530, 0.683], std [0.440, 0.186, 0.326, 0.653]
  Slot 3 (f12..f15): mean [83.05, 18.58, 62.71, 99.32], std [66.50, 28.88, 72.00, 70.50]
"""

import argparse
import json
import time
import sys

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("ERROR: paho-mqtt not found. Run: pip install paho-mqtt")
    sys.exit(1)

# ---- Normalization reference (mirrors multi_sensor_norm.json) ---
MEANS = [
    1.0, 0.0, 1.0, 1.0,
    0.0, 0.0, 0.0, 0.0,
    1.0, 0.0, 1.0, 1.0,
    0.0, 0.0, 0.0, 0.0
]
STDS = [
    0.5,  1.0, 0.5,  0.5,
    1.0,  1.0, 1.0,  1.0,
    0.05, 0.5, 0.05, 0.05,
    1.0,  1.0, 1.0,  1.0
]

# For the test rule, we inject via infer.set actions.
# infer.set expects RAW values (the C code will normalize them).

PASO_TESTS = {
    # -- Test 1: Feed each slot its exact mean (normalized=0).
    # The autoencoder should reconstruct 0→0 for all features → MSE ≈ 0.
    "normalised_zero": {
        "desc": "Send exact means for all 4 slots → expect MSE ≈ 0",
        "slots": {
            0: MEANS[0:4],
            1: MEANS[4:8],
            2: MEANS[8:12],
            3: MEANS[12:16],
        },
        "expect_anomaly": False,
    },
    # -- Test 2: Extreme outlier on slot 2 only.
    # Each feature = mean + 10*std → wildly out of distribution → MSE >> threshold.
    "extreme_outlier_slot2": {
        "desc": "Send 10-sigma outlier on slot 2, all others normal → expect ANOMALY",
        "slots": {
            0: MEANS[0:4],
            1: MEANS[4:8],
            2: [MEANS[i] + 10 * STDS[i] for i in range(8, 12)],
            3: MEANS[12:16],
        },
        "expect_anomaly": True,
    },
    # -- Test 3: Only slot 0 active.
    # Expect mask "1000", threshold = 1.140673.
    "single_slot0_normal": {
        "desc": "Only slot 0 active with normal values → expect mask 1000, OK",
        "slots": {
            0: MEANS[0:4],
        },
        "expect_anomaly": False,
    },
    # -- Test 4: Only slot 2 active (accelerometer scenario).
    # Expect mask "0010", threshold = 1.592735.
    "single_slot2_normal": {
        "desc": "Only slot 2 active with normal values → expect mask 0010, OK",
        "slots": {
            2: MEANS[8:12],
        },
        "expect_anomaly": False,
    },
    # -- Test 5: Slots 0 + 2 active (dual-sensor fusion).
    # Expect mask "1010", threshold = 1.279026.
    "fusion_slot0_slot2": {
        "desc": "Slots 0 + 2 active (fusion) with normal values → expect mask 1010, OK",
        "slots": {
            0: MEANS[0:4],
            2: MEANS[8:12],
        },
        "expect_anomaly": False,
    },
    # -- Test 6: All 4 slots active, normal values.
    # Expect mask "1111", threshold = 2.668663.
    "all_slots_normal": {
        "desc": "All 4 slots active, all normal → expect mask 1111, OK",
        "slots": {
            0: MEANS[0:4],
            1: MEANS[4:8],
            2: MEANS[8:12],
            3: MEANS[12:16],
        },
        "expect_anomaly": False,
    },
}

FEATURES_PER_SLOT = 4


def build_infer_batch(slots: dict) -> str:
    """Build a BEGINCFG rule that uses batch infer.set to inject synthetic values."""
    parts = []
    for slot_idx, values in slots.items():
        base = slot_idx * FEATURES_PER_SLOT
        for j, v in enumerate(values):
            parts.append(f"infer.set:{base + j}:{v:.6f}")
    parts.append("infer.run:log:PASO_RESULT MSE=$value")
    return " | ".join(parts)


def main():
    parser = argparse.ArgumentParser(description="Paso inference unit tests for the Pico.")
    parser.add_argument("--broker", default="localhost")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--node", default="pico-001")
    parser.add_argument("--test", default="all",
                        help="Test name to run, or 'all' (default)")
    parser.add_argument("--delay", type=float, default=1.0,
                        help="Seconds between test rule firing and next test (default: 1.0)")
    args = parser.parse_args()

    client = mqtt.Client()
    client.connect(args.broker, args.port, keepalive=30)
    client.loop_start()

    def pub(topic, payload):
        client.publish(topic, payload, qos=0)

    def send_cmd(cmd):
        pub(f"pico/{args.node}/cmd", cmd)
        time.sleep(0.2)

    def send_cfg(payload):
        pub(f"pico/{args.node}/config", payload)
        time.sleep(0.1)

    print("=" * 60)
    print(f"  Pico Inference Paso Tests")
    print(f"  Broker : {args.broker}:{args.port}  Node: {args.node}")
    print(f"  Running: {args.test}")
    print("=" * 60)

    # Enable verbose mode so tensor values appear in serial output
    send_cmd("debug.infer on")
    time.sleep(0.3)

    # Ensure project is running
    send_cmd("RUN")
    time.sleep(0.2)

    # Clear rule table first to ensure a clean state
    print("\n[PASO] Clearing rule table...")
    send_cmd("CLEAR")
    time.sleep(0.5)

    tests_to_run = PASO_TESTS if args.test == "all" else {args.test: PASO_TESTS[args.test]}

    for name, tc in tests_to_run.items():
        print(f"\n[PASO] TEST: {name}")
        print(f"       {tc['desc']}")
        expect = "ANOMALY" if tc["expect_anomaly"] else "OK"
        print(f"       Expected result: {expect}")

        # Build a batch command to inject all slots at once
        action = build_infer_batch(tc["slots"])
        # Send directly to cmd topic (bypasses rule table limits entirely)
        send_cmd(f"batch: {action}")
        
        print(f"       [> See Pico console for: [INFER] invoke=Xus | MSE=...]")
        time.sleep(args.delay)

    # Disable verbose after tests
    send_cmd("debug.infer off")

    # Clean up by sending CLEAR to remove the test rule
    print("\n[PASO] Sending CLEAR to remove PASO_TEST rule...")
    send_cmd("CLEAR")

    client.loop_stop()
    client.disconnect()

    print("\n[PASO] Done! Check the Pico serial console to validate results.")
    print("       Key things to verify:")
    print("         1. invoke=XXXus — should be 1000–10000us for INT8 TFLite model")
    print("         2. MSE values should be ~0 for normal inputs, >>threshold for outliers")
    print("         3. Mask strings should match expected slot combination")


if __name__ == "__main__":
    main()
