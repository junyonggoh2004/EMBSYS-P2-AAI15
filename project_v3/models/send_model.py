"""
send_model.py — Transfers a TFLite model + normalization params to a Pico over MQTT.

Usage:
  python send_model.py --model autoencoder_int8.tflite
  python send_model.py --model svdd.tflite --norm norm_stats.json --threshold thresholds.json
"""
import argparse
import os
import json
import time
import struct
import zlib
import paho.mqtt.client as mqtt

# ---- Defaults ----
DEFAULT_BROKER = "192.168.50.192"
DEFAULT_PORT   = 1883
DEFAULT_NODE   = "pico-001"
CHUNK_SIZE     = 512
CHUNK_DELAY    = 1.2  # seconds between chunks (matches Pico 1s sleep)

def calculate_crc32(file_path):
    with open(file_path, "rb") as f:
        data = f.read()
    return zlib.crc32(data) & 0xFFFFFFFF, len(data), data

def pack_norm_params(norm_path, thresh_path):
    """
    Pack norm_stats.json and thresholds.json into a compact binary blob.
    
    Format:
      [1B]  N = number of features
      [N*4B] means  (float32 LE)
      [N*4B] stds   (float32 LE)
      [1B]  M = number of threshold entries
      [M*8B] each = 4-char mask (ASCII) + float32 LE threshold
      [1B]  score_mode: 0=MSE (autoencoder), 1=DISTANCE (SVDD/encoder)
    """
    with open(norm_path, "r") as f:
        stats = json.load(f)

    means = stats["means"]
    stds  = stats["stds"]
    N = len(means)

    # Pack header + means + stds
    blob = struct.pack("B", N)
    for m in means:
        blob += struct.pack("<f", m)
    for s in stds:
        blob += struct.pack("<f", max(s, 1e-6))  # guard against div-by-zero

    # Pack thresholds if provided
    if thresh_path and os.path.exists(thresh_path):
        with open(thresh_path, "r") as f:
            thresh = json.load(f)

        masks = thresh.get("thresholds_by_mask", {})
        M = len(masks)
        blob += struct.pack("B", M)
        for mask_str, val in masks.items():
            # Pad/truncate mask to exactly 4 chars
            mask_4 = mask_str[:4].ljust(4, "0")
            blob += mask_4.encode("ascii")
            blob += struct.pack("<f", val)
        print(f"  Packed {M} threshold masks")
    else:
        blob += struct.pack("B", 0)  # zero thresholds

    # Pack score_mode: 0=MSE (autoencoder default), 1=DISTANCE (SVDD)
    raw_mode = stats.get("score_mode", "mse").strip().lower()
    score_mode_byte = 1 if raw_mode in ("distance", "svdd", "1") else 0
    blob += struct.pack("B", score_mode_byte)
    mode_name = "DISTANCE (SVDD)" if score_mode_byte else "MSE (Autoencoder)"
    print(f"  Score mode: {mode_name}")

    print(f"  Binary params blob: {len(blob)} bytes for {N} features")
    return blob


# ---- MQTT Callbacks ----
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print(f"Connected to broker {userdata['broker']}")
        topic = f"pico/{userdata['node']}/heartbeat"
        print(f"Subscribing to {topic} for verification...")
        client.subscribe(topic)
    else:
        print(f"Connection failed with code {rc}")

def on_message(client, userdata, msg):
    print(f"RECEIVED Heartbeat from Pico: {msg.payload.decode()}")
    userdata['pico_alive'] = True

# ---- Main Transfer ----
def send_model(args):
    node = args.node
    broker = args.broker
    port = args.port

    # --- Phase 0: Validate files ---
    if not os.path.exists(args.model):
        print(f"Error: Model file '{args.model}' not found.")
        return

    print(f"Reading model: {args.model}")
    crc, size, data = calculate_crc32(args.model)
    print(f"Size: {size} bytes, CRC32: 0x{crc:08X}")

    # Pack binary norm params if provided
    params_blob = None
    if args.norm:
        if not os.path.exists(args.norm):
            print(f"Warning: Norm file '{args.norm}' not found, skipping params.")
        else:
            print(f"Packing normalization params from: {args.norm}")
            params_blob = pack_norm_params(args.norm, args.threshold)

    # --- Phase 1: Connect to MQTT ---
    state = {'pico_alive': False, 'broker': broker, 'node': node}
    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    client.user_data_set(state)
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(broker, port, 60)
    except Exception as e:
        print(f"Failed to connect to broker at {broker}: {e}")
        return

    client.loop_start()

    print("Waiting for Pico heartbeat (to ensure same broker)...")
    for _ in range(15):
        if state['pico_alive']:
            break
        time.sleep(1)

    if not state['pico_alive']:
        print("Warning: Did not hear from Pico heartbeat. Continuing anyway!")

    # --- Phase 2: Send model ---
    topic_start = f"pico/{node}/model/start"
    payload_start = json.dumps({"size": size, "crc32": crc})
    print(f"Publishing to {topic_start}...")
    pub = client.publish(topic_start, payload_start, qos=1)
    pub.wait_for_publish()
    time.sleep(1.5)  # Give Pico time to erase flash

    print(f"Sending {size} bytes in {CHUNK_SIZE}-byte chunks...")
    for i in range(0, size, CHUNK_SIZE):
        chunk = data[i : i + CHUNK_SIZE]
        chunk_idx = i // CHUNK_SIZE
        topic_chunk = f"pico/{node}/model/chunk/{chunk_idx}"
        pub = client.publish(topic_chunk, chunk, qos=0)

        if chunk_idx % 10 == 0:
            print(f"  Sent chunk {chunk_idx}")
            pub.wait_for_publish()
        time.sleep(CHUNK_DELAY)

    topic_end = f"pico/{node}/model/end"
    print(f"Publishing to {topic_end}...")
    pub = client.publish(topic_end, b"", qos=1)
    pub.wait_for_publish()
    print("Model transfer complete!")

    # --- Phase 3: Send normalization params ---
    if params_blob:
        time.sleep(2)  # Wait for model auto-load to finish
        topic_params = f"pico/{node}/model/params"
        print(f"Publishing norm params to {topic_params} ({len(params_blob)} bytes)...")
        pub = client.publish(topic_params, params_blob, qos=1)
        pub.wait_for_publish()
        print("Normalization params sent!")

    print("\nDone! Check the Pico serial monitor for confirmation.")
    time.sleep(2)
    client.loop_stop()
    client.disconnect()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Transfer a TFLite model + normalization params to a Pico over MQTT."
    )
    parser.add_argument("--model", required=True,
                        help="Path to the .tflite model file")
    parser.add_argument("--norm", default=None,
                        help="Path to norm_stats.json (optional)")
    parser.add_argument("--threshold", default=None,
                        help="Path to thresholds.json (optional)")
    parser.add_argument("--broker", default=DEFAULT_BROKER,
                        help=f"MQTT broker IP (default: {DEFAULT_BROKER})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"MQTT broker port (default: {DEFAULT_PORT})")
    parser.add_argument("--node", default=DEFAULT_NODE,
                        help=f"Pico node ID (default: {DEFAULT_NODE})")

    args = parser.parse_args()
    send_model(args)
