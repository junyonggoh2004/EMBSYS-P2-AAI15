#!/usr/bin/env python3
"""D1 tasks 1.1 to 1.4: MQTT between two real ESP32-S3 boards.

Board A (s3-001) and board B (s3-002) run the same µT-Kernel firmware, built
with different node IDs (NODE=s3-002 sh tools/mtk.sh flash). Both stay on USB so
their consoles can be read, and both are on the broker. Every configuration
and command goes in over MQTT; the consoles and the broker are only watched.

  commands  1.4  every console command works on pico/<node>/cmd (board A only)
  data      1.1  A publishes a sensor; B follows it, gets the same bytes and acts on them
  control   1.2  rules on A stop, start, command and configure B (cmd.to, cfg.to)
  config    1.3  B is set up by one JSON message, then piece by piece; a broadcast reaches both

No wiring is needed: the sensors are GPIO inputs with internal pulls (GPIO6
and GPIO7, the pins the single-board MQTT test uses). Logs are written per
stage, with Wi-Fi network names and MAC addresses redacted.
"""

import argparse
import json
import queue
import re
import sys
import threading
import time
from pathlib import Path

import paho.mqtt.client as mqtt
import serial
from serial.tools import list_ports

BAUD = 115200
SETTLE = 2.0          # always listen this long, so late or unwanted lines are caught
ESPRESSIF_VID = 0x303A

SSID_SCAN = re.compile(r'^(\[WIFI\] )"[^"]*"( \(\d+ chars\))')
SSID_USES = re.compile(r'(this node uses )"[^"]*"')
SSID_BOOT = re.compile(r"^(\[WIFI\] Connecting to ).*")
MAC = re.compile(r"\b[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}\b")


def redact(line):
    line = SSID_SCAN.sub(r'\1"<ssid>"\2', line)
    line = SSID_USES.sub(r'\1"<ssid>"', line)
    line = SSID_BOOT.sub(r"\1<ssid>", line)
    return MAC.sub("<mac>", line)


class Rig:
    """Two consoles and the broker, read into one time-ordered event queue."""

    def __init__(self, broker, ports):
        self.events = queue.Queue()
        self.labels = {}                  # port -> "A" / "B"
        self.log = None
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.serials = {}
        for port in ports:
            ser = serial.Serial(port, BAUD, timeout=0.1)
            self.serials[port] = ser
            threading.Thread(target=self._read, args=(port, ser), daemon=True).start()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="d1-two-board-host")
        self.client.on_message = lambda c, u, m: self._event("BROKER", f"{m.topic} {self._text(m.payload)}")
        self.client.connect(broker, 1883)
        self.client.subscribe("pico/#")
        self.client.loop_start()

    @staticmethod
    def _text(payload):
        text = payload.decode(errors="replace")
        return text if len(payload) < 300 and text.isprintable() else f"<{len(payload)} bytes>"

    def _read(self, port, ser):
        buf = b""
        while not self.stop.is_set():
            try:
                buf += ser.read(512)
            except serial.SerialException:
                time.sleep(0.2)
                continue
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                self._event(port, raw.decode(errors="replace").rstrip("\r"))

    def _event(self, source, line):
        now = time.monotonic()
        self.events.put((now, source, line))
        with self.lock:
            if self.log:
                tag = self.labels.get(source, source)
                self.log.write(f"{time.strftime('%H:%M:%S')} {tag:<6} {redact(line)}\n")

    def label(self, source):
        return self.labels.get(source, source)

    def note(self, text):
        with self.lock:
            if self.log:
                self.log.write(f"{time.strftime('%H:%M:%S')} ---    {text}\n")
                self.log.flush()

    def drain(self):
        out = []
        while not self.events.empty():
            t, source, line = self.events.get()
            out.append((t, self.label(source), line))
        return out

    def publish(self, topic, payload):
        self.note(f"publish {topic}: {payload}")
        self.client.publish(topic, payload, qos=1)

    def close(self):
        self.stop.set()
        self.client.loop_stop()
        self.client.disconnect()
        for ser in self.serials.values():
            ser.close()


def seen(events, pattern):
    """pattern is 'A:regex', 'B:regex' or 'BROKER:regex'."""
    source, regex = pattern.split(":", 1)
    return next((e for e in events if e[1] == source and re.search(regex, e[2])), None)


def run_step(rig, actions, wait, patterns, pause=0.3):
    for topic, payload in actions:
        rig.publish(topic, payload)
        time.sleep(pause)
    events, start = [], time.monotonic()
    positive = [p for p in patterns if not p.startswith("!")]
    while True:
        time.sleep(0.25)
        events += rig.drain()
        elapsed = time.monotonic() - start
        if (elapsed >= SETTLE and all(seen(events, p) for p in positive)) or elapsed >= wait:
            return events


def check(step_id, description, events, patterns, extra=None):
    missing, evidence = [], []
    for p in patterns:
        if p.startswith("!"):
            hit = seen(events, p[1:])
            if hit:
                missing.append(f"{p} (appeared: {hit[1]} {hit[2]})")
            continue
        hit = seen(events, p)
        if hit:
            evidence.append(f"{hit[1]}: {hit[2]}")
        else:
            missing.append(p)
    if extra:
        ok, detail = extra(events)
        (evidence if ok else missing).append(detail)
    return {"id": step_id, "description": description, "passed": not missing,
            "missing": missing, "evidence": evidence}


# ---------- configurations (S3 pin numbers) ----------

def dig(name, pin=6, pull="down", freq=2, extra=""):
    return (f"BEGINCFG|name={name}|proto=gpio|gpio.mode=digital|gpio.pin={pin}|gpio.pull={pull}"
            f"|mode=poll|freq_hz={freq}{extra}|ENDCFG")


def rule(name, source, when, action):
    return f"BEGINRULE|name={name}|source={source}|calc=s=u8(0)|when={when}|action={action}|ENDRULE"


def line_bytes(text):
    """The data bytes after ' : ' in a sensor line, e.g. '00 01 02'."""
    return text.split(" : ", 1)[1].strip() if " : " in text else None


# ---------- stages ----------

def stage_commands(a, b):
    """1.4: every console command answers on pico/<node>/cmd. Board A only."""
    cmd, cfg = f"pico/{a}/cmd", f"pico/{a}/config"
    return [
        ("reset", "STOP and CLEAR over MQTT", [(cmd, "STOP"), (cmd, "CLEAR")], 6,
         ["A:^\\[CMD\\] CLEAR"]),
        ("tasks", "TASKS (ControlTask) lists the tasks and the new ctrl_mbf", [(cmd, "TASKS")], 8,
         ["A:^\\[TASK\\] SchedulerTask cycles=", "A:^\\[TASK\\] ctrl_mbf +free=\\d+ B dropped=0"]),
        ("app", "APP STATUS (ControlTask) reaches ApplicationTask", [(cmd, "APP STATUS")], 6,
         ["A:^\\[APP\\] request=\\d+ synthetic=0 queued=1"]),
        ("wifiscan", "WIFISCAN (ControlTask) scans while connected", [(cmd, "WIFISCAN")], 15,
         ["A:^\\[WIFI\\] \\d+ networks; this node uses"]),
        ("spiid", "SPIID (ControlTask, then BenchmarkTask) reads the flash ID or reports none",
         [(cmd, "SPIID")], 8, ["A:^\\[SPIID\\] "]),
        ("spi-args", "SPIBENCH with bad arguments is checked by ControlTask", [(cmd, "SPIBENCH 9 1")], 6,
         ["A:^\\[SPIBENCH\\] ERROR expected"]),
        ("stall", "STALL 500 (ControlTask) stalls briefly, below the watchdog limit", [(cmd, "STALL 500")], 8,
         ["A:^\\[CTRL\\] stalling ControlTask for 500 ms", "A:^\\[CTRL\\] stall over"]),
        ("window", "WINDOW STATUS (shared REPL) answers while stopped", [(cmd, "WINDOW STATUS")], 6,
         ["A:^\\[WINDOW_REPORT\\] token=00000000"]),
        ("pair", "PAIR (shared REPL) answers with its usage", [(cmd, "PAIR")], 6,
         ["A:^\\[PAIR\\] request=-?\\d+"]),
        ("trigger", "TRIGGER (shared REPL) reaches the scheduler", [(cmd, "TRIGGER NOPE")], 6,
         ["A:^\\[TIMING\\] trigger=NOPE armed=0"]),
        ("rules-add", "A rule over the config topic, listed by SHOWRULES over the command topic",
         [(cfg, dig("DIG")), (cfg, rule("R_LOW", "DIG", "s==0", "log:pin low")), (cmd, "SHOWRULES")], 6,
         ["A:^\\[RULE\\] added name=R_LOW", "A:^\\[RULE\\] count=1", "A:^  #0 name=R_LOW src=DIG"]),
        ("rules-clear", "CLEARRULES, then SHOWRULES shows none", [(cmd, "CLEARRULES"), (cmd, "SHOWRULES")], 6,
         ["A:^\\[RULE\\] count=0"]),
        ("help", "HELP (shared REPL)", [(cmd, "HELP")], 6, ["A:^Commands:"]),
        ("ml", "ML SHOW (ML parser)", [(cmd, "ML SHOW")], 6, ["A:^ML: model="]),
        ("config-refused", "Configuration on the command topic is pointed to the config topic",
         [(cmd, "BEGINCFG|name=X|proto=gpio|gpio.pin=6|ENDCFG")], 6,
         [f"A:^\\[CMD\\] configuration goes to pico/{a}/config", "!A:added #\\d+: X"]),
        ("unknown", "Unknown text is still rejected", [(cmd, "FOO")], 6, ["A:^\\[CMD\\] Unknown: FOO"]),
        ("clear", "CLEAR", [(cmd, "CLEAR")], 6, ["A:^\\[CMD\\] CLEAR"]),
        ("wifidrop", "WIFIDROP (ControlTask) drops Wi-Fi for 10 s; the node reconnects by itself",
         [(cmd, "WIFIDROP")], 45,
         ["A:^\\[WIFI\\] dropping the connection for 10 s", "A:^\\[MQTT\\] Status: Connected",
          f"BROKER:^pico/{a}/presence "]),
    ]


def stage_data(a, b):
    """1.1: B follows A's sensor and acts on it."""
    ca, cb, ka, kb = f"pico/{a}/config", f"pico/{b}/config", f"pico/{a}/cmd", f"pico/{b}/cmd"

    def same_bytes(level, after):
        """Bytes B bridged since A printed `after` are bytes A published, starting with `level`."""
        def fn(events):
            start = next((e[0] for e in events if e[1] == "A" and re.search(after, e[2])), None)
            if start is None:
                return False, f"A never printed {after}"
            events = [e for e in events if e[0] > start]
            # A prints each line on its console before publishing it, so its console
            # is the full record; the broker copy can reach the host after B's.
            sent = {line_bytes(e[2]) for e in events
                    if (e[1] == "BROKER" and e[2].startswith(f"pico/{a}/sensor/DIG/line "))
                    or (e[1] == "A" and e[2].startswith("proto=gpio src=DIG "))}
            got = {line_bytes(e[2]) for e in events if e[1] == "B" and e[2].startswith("proto=mqtt src=REM ")}
            sent.discard(None)
            got.discard(None)
            ok = bool(got) and got <= sent and all(g.split()[0] == level for g in got)
            return ok, f"bytes A published {sorted(sent)}; bytes B bridged {sorted(got)}; first byte {level}"
        return fn

    return [
        ("reset", "Both nodes stopped and cleared over MQTT",
         [(ka, "STOP"), (ka, "CLEAR"), (kb, "STOP"), (kb, "CLEAR")], 6,
         ["A:^\\[CMD\\] CLEAR", "B:^\\[CMD\\] CLEAR"]),
        ("a-sensor", "A gets a GPIO input (GPIO6, pulled down) over MQTT", [(ca, dig("DIG"))], 6,
         ["A:^\\[CFG\\] added #\\d+: DIG", f"BROKER:^pico/{a}/status/config OK:DIG"]),
        ("b-follow", "B follows A's DIG as its own sensor REM, with rules on its value",
         [(cb, f"BEGINCFG|name=REM|proto=mqtt|mode=poll|freq_hz=2|remote_node={a}|remote_source=DIG|ENDCFG"),
          (cb, rule("R_LOW", "REM", "s==0", "log:A reads low")),
          (cb, rule("R_HIGH", "REM", "s==1", "log:A reads high")), (kb, "RUN")], 8,
         [f"B:^\\[SCHED\\] added MQTT-remote: local=REM <- {a}/DIG", "B:^\\[RULE\\] added name=R_LOW",
          "B:^\\[RULE\\] added name=R_HIGH", "B:^\\[CMD\\] RUN"]),
        ("a-run", "A samples; B receives the same bytes and its rule acts on them", [(ka, "RUN")], 10,
         [f"BROKER:^pico/{a}/sensor/DIG/line proto=gpio src=DIG", f"B:^\\[BRIDGE\\] on_remote_line: {a}/DIG -> local=REM",
          "B:^proto=mqtt src=REM len=\\d+ ts=\\d+ : ", "B:^\\[RULE\\] R_LOW: A reads low",
          "BROKER:^pico/rule/R_LOW/event ", "!B:^\\[RULE\\] R_HIGH"], same_bytes("00", r"^\[CMD\] RUN")),
        ("a-change", "A's input is reconfigured live (pull-up); B sees the new value and the other rule fires",
         [(ka, "STOP"), (ka, "CLEAR"), (ca, dig("DIG", pull="up")), (ka, "RUN")], 12,
         ["A:^\\[CFG\\] added #\\d+: DIG", "B:^\\[RULE\\] R_HIGH: A reads high"],
         same_bytes("01", r"^\[CMD\] RUN")),
        ("stop", "Both stopped", [(ka, "STOP"), (kb, "STOP")], 6, ["A:^\\[CMD\\] STOP", "B:^\\[CMD\\] STOP"]),
    ]


def stage_control(a, b):
    """1.2: rules on A control and configure B."""
    ca, cb, ka, kb = f"pico/{a}/config", f"pico/{b}/config", f"pico/{a}/cmd", f"pico/{b}/cmd"
    # A one-shot input: each RUN or TRIGGER samples it once, so its rule fires once.
    once = dig("GO", freq=1, extra="|timing.mode=one_shot")

    def b_quiet_after_stop(events):
        stop = next((t for t, s, line in events if s == "B" and line.startswith("[CMD] STOP")), None)
        if stop is None:
            return False, "B never printed [CMD] STOP"
        late = [line for t, s, line in events if s == "B" and t > stop + 1.0 and line.startswith("proto=gpio src=BDIG")]
        return not late, f"B sensor lines more than 1 s after STOP: {len(late)}"

    def b_lines_after_run(events):
        run = next((t for t, s, line in events if s == "B" and line.startswith("[CMD] RUN")), None)
        if run is None:
            return False, "B never printed [CMD] RUN"
        after = [line for t, s, line in events if s == "B" and t > run and line.startswith("proto=gpio src=BDIG")]
        return bool(after), f"B sensor lines after RUN: {len(after)}"

    cfg_text = (f"cfg.to:{b}:BEGINCFG\\|name=LED\\|proto=gpio\\|gpio.mode=digital\\|gpio.pin=8"
                "\\|mode=poll\\|freq_hz=1\\|ENDCFG")
    cfg_json = f"cfg.to:{b}:" + json.dumps({"configs": [{"name": "LEDJ", "proto": "gpio", "mode": "poll", "freq_hz": 1,
                                                       "gpio": {"pin": 8, "mode": "digital"}}]}, separators=(",", ":"))
    # The bare-word form of the reference project's own example (test_json/h206_remote.json),
    # which a JSON rule needs because it cannot hold nested quotes (D-11).
    cfg_relaxed = f"cfg.to:{b}:{{configs:[{{name:LEDR,proto:gpio,mode:poll,freq_hz:1,gpio:{{pin:8,mode:digital}}}}]}}"
    return [
        ("reset", "Both nodes stopped and cleared",
         [(ka, "STOP"), (ka, "CLEAR"), (kb, "STOP"), (kb, "CLEAR")], 6,
         ["A:^\\[CMD\\] CLEAR", "B:^\\[CMD\\] CLEAR"]),
        ("b-running", "B runs its own sensor", [(cb, dig("BDIG")), (kb, "RUN")], 6,
         ["B:^\\[CFG\\] added #\\d+: BDIG", "B:^proto=gpio src=BDIG"]),
        ("stop-b", "A rule on A stops B (cmd.to); B's sampling stops",
         [(ca, once), (ca, rule("R_STOP_B", "GO", "s==0", f"cmd.to:{b}:STOP")), (ka, "RUN")], 8,
         [f"A:^\\[RULE\\] R_STOP_B: cmd.to -> node={b} cmd='STOP'", f"BROKER:^pico/{b}/cmd STOP$",
          "B:^\\[CMD\\] STOP"], b_quiet_after_stop),
        ("start-b", "A rule on A starts B again (cmd.to RUN)",
         [(ka, "STOP"), (ka, "CLEARRULES"), (ca, rule("R_RUN_B", "GO", "s==0", f"cmd.to:{b}:RUN")),
          (ka, "RUN")], 8,
         [f"A:^\\[RULE\\] R_RUN_B: cmd.to -> node={b} cmd='RUN'", "B:^\\[CMD\\] RUN"], b_lines_after_run),
        ("platform-b", "A sends B a platform command (TASKS) and an ML command; TRIGGER over MQTT re-arms A",
         [(ka, "STOP"), (ka, "CLEARRULES"), (ca, rule("R_TASKS_B", "GO", "s==0", f"cmd.to:{b}:TASKS")),
          (ca, rule("R_ML_B", "GO", "s==0", f"cmd.to:{b}:ML SHOW")), (ka, "RUN"), (ka, "TRIGGER GO")], 10,
         [f"BROKER:^pico/{b}/cmd TASKS$", "B:^\\[TASK\\] SchedulerTask cycles=",
          f"BROKER:^pico/{b}/cmd ML SHOW$", "B:^ML: model=", "A:^\\[TIMING\\] trigger=GO armed=1"]),
        ("cfg-text", "A configures B with a text block (cfg.to); B applies it unchanged (D-11)",
         [(ka, "STOP"), (ka, "CLEARRULES"), (kb, "STOP"), (ca, rule("R_CFG_B", "GO", "s==0", cfg_text)), (ka, "RUN")], 8,
         [f"A:^\\[RULE\\] R_CFG_B: cfg.to -> node={b}", "B:^\\[CFG\\] added #\\d+: LED$",
          f"BROKER:^pico/{b}/status/config OK:LED$", "!B:added #\\d+: LED\\\\"]),
        ("cfg-json", "A configures B with JSON (cfg.to); B converts and applies it",
         [(ka, "STOP"), (ka, "CLEARRULES"), (ca, rule("R_JSON_B", "GO", "s==0", cfg_json)), (ka, "RUN")], 8,
         [f"A:^\\[RULE\\] R_JSON_B: cfg.to -> node={b}", "B:^\\[CFG\\] added #\\d+: LEDJ",
          f"BROKER:^pico/{b}/status/config OK:LEDJ"]),
        ("cfg-relaxed", "A configures B with bare-word JSON, the reference example's form (D-11)",
         [(ka, "STOP"), (ka, "CLEARRULES"), (ca, rule("R_RELAX_B", "GO", "s==0", cfg_relaxed)), (ka, "RUN")], 8,
         [f"A:^\\[RULE\\] R_RELAX_B: cfg.to -> node={b}", "B:^\\[CFG\\] added #\\d+: LEDR",
          f"BROKER:^pico/{b}/status/config OK:LEDR"]),
        ("b-show", "B lists what A configured", [(kb, "SHOW")], 6,
         ["B:name=LED proto=gpio", "B:name=LEDJ proto=gpio", "B:name=LEDR proto=gpio"]),
        ("stop", "Both stopped and cleared", [(ka, "STOP"), (ka, "CLEAR"), (kb, "STOP"), (kb, "CLEAR")], 6,
         ["A:^\\[CMD\\] CLEAR", "B:^\\[CMD\\] CLEAR"]),
    ]


def stage_config(a, b):
    """1.3: one message, then piece by piece; the same result. A broadcast reaches both."""
    cb, kb, ka = f"pico/{b}/config", f"pico/{b}/cmd", f"pico/{a}/cmd"
    c1 = {"name": "C1", "proto": "gpio", "mode": "poll", "freq_hz": 1,
          "gpio": {"pin": 6, "pull": "down", "mode": "digital"}}
    c2 = {"name": "C2", "proto": "gpio", "mode": "poll", "freq_hz": 2,
          "gpio": {"pin": 7, "pull": "up", "mode": "digital"}}
    r1 = {"name": "R_C1", "source": "C1", "calc": ["s=u8(0)"], "when": "s==0", "action": "log:C1 low"}
    whole = json.dumps({"configs": [c1, c2], "rules": [r1]})
    pieces = [json.dumps({"type": "config", **c1}), json.dumps({"type": "config", **c2}),
              json.dumps({"type": "rule", **r1})]
    shows = {}

    def dump(events):
        return sorted(re.sub(r"\d+ms", "", line) for t, s, line in events
                      if s == "B" and ("name=C" in line or "R_C1" in line) and not line.startswith("[CFG]")
                      and not line.startswith("[JSON]") and not line.startswith("[RULE] added"))

    def keep(name):
        def fn(events):
            shows[name] = dump(events)
            return bool(shows[name]), f"{name} listing: {len(shows[name])} lines"
        return fn

    def same(events):
        shows["pieces"] = dump(events)
        return shows.get("whole") == shows["pieces"], \
            f"one message and piece by piece list the same setup: {shows.get('whole') == shows['pieces']}"

    acks = ["B:^\\[CFG\\] added #\\d+: C1", "B:^\\[CFG\\] added #\\d+: C2", "B:^\\[RULE\\] added name=R_C1",
            f"BROKER:^pico/{b}/status/config OK:C1", f"BROKER:^pico/{b}/status/config OK:C2",
            f"BROKER:^pico/{b}/status/rule OK:R_C1"]
    bc = "BEGINCFG|name=BC|proto=gpio|gpio.mode=digital|gpio.pin=6|gpio.pull=down|mode=poll|freq_hz=1|ENDCFG"
    return [
        ("reset", "Both nodes stopped and cleared",
         [(ka, "STOP"), (ka, "CLEAR"), (kb, "STOP"), (kb, "CLEAR")], 6,
         ["A:^\\[CMD\\] CLEAR", "B:^\\[CMD\\] CLEAR"]),
        ("whole", "Two sensors and a rule in one JSON message", [(cb, whole)], 8, acks),
        ("whole-show", "SHOW lists the setup", [(kb, "SHOW")], 6, ["B:name=C1", "B:name=C2", "B:R_C1"], keep("whole")),
        ("clear", "CLEAR", [(kb, "CLEAR")], 6, ["B:^\\[CMD\\] CLEAR"]),
        ("pieces", "The same setup as three separate JSON messages", [(cb, p) for p in pieces], 10, acks),
        ("pieces-show", "SHOW lists the same setup as the single message", [(kb, "SHOW")], 6,
         ["B:name=C1", "B:name=C2", "B:R_C1"], same),
        ("live", "The setup runs: the rule fires", [(kb, "RUN")], 8, ["B:^\\[RULE\\] R_C1: C1 low"]),
        ("broadcast", "One config on pico/broadcast/config reaches both nodes",
         [(kb, "STOP"), ("pico/broadcast/config", bc)], 8,
         ["A:^\\[CFG\\] added #\\d+: BC", "B:^\\[CFG\\] added #\\d+: BC",
          f"BROKER:^pico/{a}/status/config OK:BC", f"BROKER:^pico/{b}/status/config OK:BC"]),
        ("stop", "Both cleared", [(ka, "CLEAR"), (kb, "CLEAR")], 6, ["A:^\\[CMD\\] CLEAR", "B:^\\[CMD\\] CLEAR"]),
    ]


STAGES = {"commands": (stage_commands, False), "data": (stage_data, True),
          "control": (stage_control, True), "config": (stage_config, True)}


def espressif_ports():
    return sorted(p.device for p in list_ports.comports() if p.vid == ESPRESSIF_VID)


def identify(rig, node, candidates):
    """Which console answers a SHOW sent to node's command topic."""
    rig.drain()
    rig.client.publish(f"pico/{node}/cmd", "SHOW", qos=1)
    end = time.monotonic() + 8
    while time.monotonic() < end:
        time.sleep(0.25)
        for t, source, line in rig.drain():
            if source in candidates and line.startswith("[CMD] SHOW"):
                return source
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--broker", required=True, help="broker address, the boards' MQTT_HOST")
    parser.add_argument("--stage", default="all", choices=["all", *STAGES])
    parser.add_argument("--node-a", default="s3-001")
    parser.add_argument("--node-b", default="s3-002")
    parser.add_argument("--port-a", help="board A's serial port (default: found by asking each board)")
    parser.add_argument("--port-b", help="board B's serial port")
    parser.add_argument("--log-dir", default="parity/logs/mtk/d1-mqtt")
    args = parser.parse_args()

    stages = list(STAGES) if args.stage == "all" else [args.stage]
    need_b = any(STAGES[s][1] for s in stages)
    ports = [p for p in (args.port_a, args.port_b) if p] or espressif_ports()
    if not ports or (need_b and len(ports) < 2):
        sys.exit(f"need {'two boards' if need_b else 'a board'} on USB; found {ports or 'none'}")

    rig = Rig(args.broker, ports)
    time.sleep(1.0)
    if args.port_a:
        rig.labels[args.port_a] = "A"
    else:
        found = identify(rig, args.node_a, ports)
        if not found:
            sys.exit(f"no board answered on pico/{args.node_a}/cmd; is it flashed as {args.node_a} and on the broker?")
        rig.labels[found] = "A"
    if need_b:
        if args.port_b:
            rig.labels[args.port_b] = "B"
        else:
            rest = [p for p in ports if p not in rig.labels]
            found = identify(rig, args.node_b, rest)
            if not found:
                sys.exit(f"no board answered on pico/{args.node_b}/cmd; flash it with NODE={args.node_b} sh tools/mtk.sh flash")
            rig.labels[found] = "B"
    print("Boards: " + ", ".join(f"{label} = {port}" for port, label in sorted(rig.labels.items(), key=lambda x: x[1])))

    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    total = passed = 0
    for name in stages:
        build, _ = STAGES[name]
        path = log_dir / f"{name}.log"
        results = []
        with open(path, "w") as log:
            with rig.lock:
                rig.log = log
            rig.note(f"stage {name}: A={args.node_a} B={args.node_b}")
            rig.drain()
            for step in build(args.node_a, args.node_b):
                step_id, description, actions, wait, patterns = step[:5]
                extra = step[5] if len(step) > 5 else None
                rig.note(f"step {step_id}: {description}")
                events = run_step(rig, actions, wait, patterns)
                results.append(check(step_id, description, events, patterns, extra))
            with rig.lock:
                rig.log = None
        print(f"\n== {name}")
        for r in results:
            print(f"{'PASS' if r['passed'] else 'FAIL'}  {r['id']:<15} {r['description']}")
            for line in r["evidence"]:
                print(f"        seen: {line[:120]}")
            for line in r["missing"]:
                print(f"        MISSING: {line[:160]}")
        n = sum(r["passed"] for r in results)
        print(f"{name}: {n}/{len(results)} passed. Log: {path}")
        total += len(results)
        passed += n
    rig.close()
    print(f"\n{passed}/{total} passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
