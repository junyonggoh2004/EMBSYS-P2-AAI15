#!/usr/bin/env python3
"""Drive configpico2wedge over Wi-Fi/MQTT and check the serial log and the broker.

The board stays on USB so its serial output can be read, while every command
and configuration goes in over MQTT. The host also plays a second node,
pico-002, for the cmd.to, cfg.to and bridging steps. Needs paho-mqtt and a
broker the board can reach (MQTT_HOST in wifi_credentials.h).
"""

import argparse
import queue
import subprocess
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt
import serial

from parity_test import BAUD, all_seen, capture_after_replug, check, find_port, read_for

NODE = "pico-001"
PEER = "pico-002"  # played by the host
CFG = f"pico/{NODE}/config"
CMD = f"pico/{NODE}/cmd"
MODELS = Path(__file__).resolve().parent.parent / "project_v3" / "models"
SETTLE = 2.0  # always listen this long, so late or unwanted lines are caught

BOOT_CHECKS = [
    r"^WiFi connected",
    r"^\[MQTT\] Connecting to ",
    r"^\[MQTT\] Subscribed OK to: node-wildcard",
]

DIG = ("BEGINCFG|name=DIG|proto=gpio|gpio.mode=digital|gpio.pin=22|gpio.pull=down"
       "|mode=poll|freq_hz=2|ENDCFG")
JSON_CFG = ('{"configs":[{"name":"JDIG","proto":"gpio","mode":"poll","freq_hz":1,'
            '"gpio":{"pin":22,"pull":"down","mode":"digital"}}],'
            '"rules":[{"name":"R_JSON","source":"JDIG","calc":["s=u8(0)"],"when":"s==0",'
            '"action":"log:json rule"}]}')

# (id, description, topic, payload, max seconds, patterns)
# Serial lines and broker messages are checked together; a broker message is
# seen as "BROKER <topic> <payload>". A topic of None only listens, and
# "send_model.py" runs the upload script.
STEPS = [
    ("connect", "Board is on the broker, announces itself and sends heartbeats", None, None, 15.0,
     [r"^\[MQTT\] Status: Connected",
      rf"^BROKER pico/{NODE}/presence \{{\"node\":\"{NODE}\"",
      rf"^BROKER pico/{NODE}/heartbeat \{{\"uptime_s\":\d+\}}"]),
    ("cfg-text", "Text config over MQTT is applied and acknowledged", CFG, DIG, 6.0,
     [r"^\[CFG\] added #\d+: DIG", rf"^BROKER pico/{NODE}/status/config OK:DIG"]),
    ("rule-text", "Text rule over MQTT is applied and acknowledged", CFG,
     "BEGINRULE|name=R_LOG|source=DIG|calc=s=u8(0)|when=s==0|action=log:pin low|ENDRULE", 6.0,
     [r"^\[RULE\] added name=R_LOG", rf"^BROKER pico/{NODE}/status/rule OK:R_LOG"]),
    ("cmd-run", "RUN over MQTT starts sampling; telemetry and rule events reach the broker",
     CMD, "RUN", 8.0,
     [r"^\[CMD\] RUN", r"^proto=gpio src=DIG len=3", r"^\[RULE\] R_LOG: pin low",
      rf"^BROKER pico/{NODE}/sensor/DIG/line proto=gpio src=DIG len=3 ts=\d+ :",
      r"^BROKER pico/rule/R_LOG/event .*msg=pin low"]),
    ("cmd-show", "SHOW over MQTT lists the sensor", CMD, "SHOW", 6.0,
     [r"^\[CMD\] SHOW", r"name=DIG"]),
    ("cmd-stop", "STOP over MQTT stops sampling", CMD, "STOP", 6.0, [r"^\[CMD\] STOP"]),
    ("cmd-clear", "CLEAR over MQTT removes sensors and rules", CMD, "CLEAR", 6.0,
     [r"^\[CMD\] CLEAR"]),
    ("cfg-json", "JSON config with a sensor and a rule is converted and applied", CFG, JSON_CFG, 8.0,
     [r"^\[CFG\] mqtt_json_try_convert_to_inline -> [1-9]", r"^\[CFG\] added #\d+: JDIG",
      r"^\[RULE\] added name=R_JSON",
      rf"^BROKER pico/{NODE}/status/config OK:JDIG", rf"^BROKER pico/{NODE}/status/rule OK:R_JSON"]),
    ("json-run", "The JSON rule fires", CMD, "RUN", 8.0,
     [r"^proto=gpio src=JDIG len=3", r"^\[RULE\] R_JSON: json rule"]),
    ("json-stop", "STOP", CMD, "STOP", 6.0, [r"^\[CMD\] STOP"]),
    ("cmd-unknown", "An unknown command over MQTT is rejected", CMD, "FOO", 6.0,
     [r"^\[CMD\] Unknown: FOO"]),
    ("cmd-clear-2", "CLEAR", CMD, "CLEAR", 6.0, [r"^\[CMD\] CLEAR"]),
    # P-28: remote actions, with the host as pico-002
    ("to-cfg", "Sensor for the remote-action rules", CFG, DIG, 6.0, [r"^\[CFG\] added #\d+: DIG"]),
    ("to-rule-cmd", "Rule with a cmd.to action is accepted", CFG,
     f"BEGINRULE|name=R_CMDTO|source=DIG|calc=s=u8(0)|when=s==0|action=cmd.to:{PEER}:RUN|ENDRULE", 6.0,
     [r"^\[RULE\] added name=R_CMDTO"]),
    ("to-rule-cfg", "Rule with a cfg.to action is accepted", CFG,
     rf"BEGINRULE|name=R_CFGTO|source=DIG|calc=s=u8(0)|when=s==0|action=cfg.to:{PEER}:BEGINCFG\|name=LED"
     r"\|proto=gpio\|gpio.mode=digital\|gpio.pin=25\|ENDCFG|ENDRULE", 6.0,
     [r"^\[RULE\] added name=R_CFGTO"]),
    ("to-run", "Both rules send to pico-002 through the broker", CMD, "RUN", 8.0,
     [rf"^\[RULE\] R_CMDTO: cmd.to -> node={PEER} cmd='RUN'", rf"^\[RULE\] R_CFGTO: cfg.to -> node={PEER}",
      rf"^BROKER pico/{PEER}/cmd RUN$", rf"^BROKER pico/{PEER}/config .*BEGINCFG.*name=LED"]),
    ("to-stop", "STOP", CMD, "STOP", 6.0, [r"^\[CMD\] STOP"]),
    ("to-clear", "CLEAR", CMD, "CLEAR", 6.0, [r"^\[CMD\] CLEAR"]),
    # P-29: model upload, then inference on the uploaded model
    ("model-upload", "send_model.py uploads the model and parameters; the board verifies and loads them",
     "send_model.py", None, 120.0,
     [r"^\[MODEL\] Transfer starting: 16440 bytes", r"^\[MODEL\] Transfer complete! 16440 bytes verified",
      r"^\[INFER\] Auto-load successful", r"^\[INFER\] Model loaded: 16 inputs, 16 outputs",
      rf"^\[MQTT\] Norm params on: pico/{NODE}/model/params", r"^\[INFER\] Loaded 16 normalization params",
      r"!CRC32 mismatch", r"!Auto-load FAILED"]),
    ("infer-cfg", "Sensor for the inference rule", CFG, DIG, 6.0, [r"^\[CFG\] added #\d+: DIG"]),
    ("infer-rule", "Rule that runs inference is accepted", CFG,
     r"BEGINRULE|name=R_INF|source=DIG|calc=s=u8(0)|when=1|action=batch: infer.set:0:1.0 \| infer.run|ENDRULE",
     6.0, [r"^\[RULE\] added name=R_INF"]),
    ("infer-run", "Inference runs on the uploaded model", CMD, "RUN", 8.0,
     [r"^\[INFER\] invoke=\d+us", r"!Invoke failed"]),
    ("infer-stop", "STOP", CMD, "STOP", 6.0, [r"^\[CMD\] STOP"]),
    ("infer-clear", "CLEAR", CMD, "CLEAR", 6.0, [r"^\[CMD\] CLEAR"]),
    # P-28: bridging a sensor published by pico-002
    ("bridge-cfg", "Remote MQTT sensor following pico-002/LIGHT is accepted", CFG,
     f"BEGINCFG|name=REM|proto=mqtt|mode=poll|freq_hz=1|remote_node={PEER}|remote_source=LIGHT|ENDCFG", 6.0,
     [rf"^\[SCHED\] added MQTT-remote: local=REM <- {PEER}/LIGHT"]),
    ("bridge-line", "A line from pico-002 is bridged into the local sensor REM",
     f"pico/{PEER}/sensor/LIGHT/line", "proto=i2c src=LIGHT len=2 ts=1 : 12 34", 8.0,
     [rf"^\[BRIDGE\] on_remote_line: {PEER}/LIGHT -> local=REM", r"^proto=mqtt src=REM len=2 ts=\d+ : 12 34"]),
    ("bridge-clear", "CLEAR", CMD, "CLEAR", 6.0, [r"^\[CMD\] CLEAR"]),
]


def drain(inbox, log):
    msgs = []
    while not inbox.empty():
        topic, payload = inbox.get()
        text = payload.decode(errors="replace")
        if len(payload) >= 300 or not text.isprintable():
            text = f"<{len(payload)} bytes>"  # model chunks and parameters are binary
        line = f"BROKER {topic} {text}"
        log.write(f"{time.strftime('%H:%M:%S')} {line}\n")
        msgs.append(line)
    return msgs


def run_step(ser, inbox, wait, patterns, log):
    """Listen for at least SETTLE seconds, then until every pattern is seen or `wait` runs out."""
    lines, start = [], time.monotonic()
    while True:
        lines += read_for(ser, 0.5, log)
        lines += drain(inbox, log)
        elapsed = time.monotonic() - start
        if (elapsed >= SETTLE and all_seen(lines, patterns)) or elapsed >= wait:
            return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--broker", required=True, help="broker address, the same as MQTT_HOST")
    parser.add_argument("--port", help="serial port (default: first usbmodem/ACM port found)")
    parser.add_argument("--log", default="parity/logs/mqtt.log", help="raw serial and broker log")
    parser.add_argument("--boot", action="store_true",
                        help="wait for the board to be replugged and check its Wi-Fi and MQTT boot log first")
    args = parser.parse_args()

    inbox = queue.Queue()
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="parity-host")
    client.on_message = lambda c, u, m: inbox.put((m.topic, m.payload))
    client.connect(args.broker, 1883)
    client.subscribe("pico/#")
    client.loop_start()

    log_path = Path(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    results = []

    with open(log_path, "w") as log:
        if args.boot:
            ser, boot_lines = capture_after_replug(25.0, log)
            results.append(check("boot", "Board joins Wi-Fi, connects to the broker and subscribes",
                                 boot_lines, BOOT_CHECKS))
        else:
            port = args.port or find_port()
            if not port:
                sys.exit("no serial port found; is the board plugged in and not held by another terminal?")
            ser = serial.Serial(port, BAUD, timeout=0.1)
            read_for(ser, 1.0, log)

        for step_id, description, topic, payload, wait, patterns in STEPS:
            log.write(f"{time.strftime('%H:%M:%S')} --- {step_id}: {topic} {payload}\n")
            log.flush()
            proc = None
            if topic == "send_model.py":
                proc = subprocess.Popen(
                    [sys.executable, "send_model.py", "--broker", args.broker, "--node", NODE,
                     "--model", "autoencoder_int8 (2).tflite", "--norm", "norm_stats (2).json",
                     "--threshold", "thresholds (2).json"],
                    cwd=MODELS, stdout=log, stderr=subprocess.STDOUT)
            elif topic:
                client.publish(topic, payload, qos=1)
            lines = run_step(ser, inbox, wait, patterns, log)
            if proc:
                proc.wait(timeout=30)
            results.append(check(step_id, description, lines, patterns))
        ser.close()

    client.loop_stop()
    client.disconnect()

    for r in results:
        status = "PASS" if r["passed"] else "FAIL"
        print(f"{status}  {r['id']:<16} {r['description']}")
        for line in r["evidence"]:
            print(f"        seen: {line[:110]}")
        for pattern in r["missing"]:
            print(f"        MISSING: {pattern}")
    passed = sum(r["passed"] for r in results)
    print(f"\n{passed}/{len(results)} passed. Raw log: {log_path}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
