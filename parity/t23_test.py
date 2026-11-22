#!/usr/bin/env python3
"""D2-9 (T-23): flash event logging, live sampling and inference together.

One HC-SR04 (Trig GPIO4, Echo GPIO5) read twice a second feeds sine-int8, a
GPIO input on GPIO8 is read every 100 ms for the timing figures, and an
application event is sent every 250 ms with flash logging on (LOG ON), so
ApplicationTask, the log handoff and BenchmarkTask's flash writes run while
SchedulerTask samples and InferenceTask runs the model. With a W25Q32 on the
D2 pins each event is written and read back until the 30-slot area is full;
without one every write reports the missing module. The run reports sampling
mean, p95, maximum and skips, the flash write time, log-queue drops, stack,
heap and resets.

    .venv/bin/python parity/t23_test.py --port <port> [--seconds 60] [--log <file>]
"""
import argparse
import json
import re
import statistics
import sys
import time
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from d5_bench import redact  # noqa: E402

SETUP = [
    "STOP", "CLEAR", "ML STOP",
    "BEGINCFG|name=DISTANCE|proto=gpio|gpio.mode=pulse|mode=poll|freq_hz=2|timing.timeout_ms=100|gpio.trig=4"
    "|gpio.echo=5|gpio.pull=down|gpio.trig_us=10|gpio.pulse_timeout_us=30000|ENDCFG",
    "BEGINCFG|name=T100|proto=gpio|gpio.mode=digital|gpio.pin=8|gpio.pull=up|mode=poll"
    "|timing.mode=periodic|timing.interval_ms=100|ENDCFG",
    "ML MODEL sine-int8", "ML RESET", "ML BIND 0 DISTANCE distance_cm 2 400",
    "APP RESET", "LOG ON", "ML RUN",
]


class Board:
    def __init__(self, port, log):
        self.ser = serial.Serial(port, 115200, timeout=0.05)
        self.log = log
        self.buf = b""
        self.lines = []

    def read(self, secs):
        end = time.monotonic() + secs
        out = []
        while time.monotonic() < end:
            self.buf += self.ser.read(1024)
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                line = re.sub(r"(Connecting to ).*", r"\1<ssid>", redact(raw.decode(errors="replace").rstrip("\r")))
                self.log.write(f"{time.strftime('%H:%M:%S')} {line}\n")
                out.append(line)
        self.lines += out
        return out

    def cmd(self, text, secs=0.6):
        self.log.write(f"> {text}\n")
        self.ser.write((text + "\r\n").encode())
        return self.read(secs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--seconds", type=int, default=60)
    ap.add_argument("--log", default="parity/logs/mtk/t23.log")
    a = ap.parse_args()
    Path(a.log).parent.mkdir(parents=True, exist_ok=True)
    with open(a.log, "w") as log:
        b = Board(a.port, log)
        b.read(0.5)
        for c in SETUP:
            b.cmd(c)
        start = len(b.lines)
        b.cmd("RUN", 0.2)
        sent, i, t_end = 0, 0, time.monotonic() + a.seconds
        while time.monotonic() < t_end:
            i += 1
            b.cmd(f"APP EVENT {1000 + i} {i} {'entry' if i % 2 else 'exit'}", 0.25)
            sent += 1
        run = b.lines[start:]
        b.cmd("STOP", 1.0)
        b.read(3.0)                                    # let the log queue drain
        tail = b.cmd("LOG STATUS") + b.cmd("APP STATUS") + b.cmd("TASKS", 2.0)
        b.cmd("LOG OFF"); b.cmd("ML STOP"); b.cmd("CLEAR")

    ts = [int(m.group(1)) for m in (re.search(r"src=T100 .*ts=(\d+)", l) for l in run) if m]
    iv = [y - x for x, y in zip(ts, ts[1:])]
    dist = [l for l in run if l.startswith('{"sensor":"DISTANCE"')]
    dist_ok = [l for l in dist if '"status":"OK"' in l]
    ml = [l for l in run if l.startswith('{"type":"ml"')]
    ml_ok = [l for l in ml if '"status":"OK"' in l]
    inv = [int(m.group(1)) for m in (re.search(r'"invoke_us":(\d+)', l) for l in ml_ok) if m]
    app = [l for l in run if l.startswith("[APP] id=")]
    logw = [int(m.group(1)) for m in (re.search(r"^\[LOG\] seq=.* write_us=(\d+)", l) for l in b.lines) if m]
    stored = sum(1 for l in b.lines if re.search(r"^\[LOG\] seq=.*readback=verified", l))
    notst = [l for l in b.lines if re.search(r"^\[LOG\] seq=.*not stored", l)]
    full = sum(1 for l in notst if "log full" in l)
    status = next((l for l in tail if l.startswith("[LOG] enabled")), "")
    dropped = int(re.search(r"dropped=(\d+)", status).group(1)) if status else -1
    occ = next((l for l in tail if l.startswith("[APP] status")), "")
    stacks = [int(x) for x in re.findall(r"stack_free=(\d+)", " ".join(tail))]
    heap = re.search(r"heap_free=(\d+) B min_free=(\d+)", " ".join(tail))
    mbf_drops = {m.group(1): int(m.group(2)) for m in
                 (re.search(r"\[TASK\] (\w+_mbf)\s+free=-?\d+ B dropped=(\d+)", l) for l in tail) if m}
    resets = sum(1 for l in b.lines if re.search(r"^\[BOOT\]|ESP-ROM:|rst:0x", l))

    res = {
        "seconds": a.seconds,
        "sampling_T100": {"intervals": len(iv), "mean_ms": round(statistics.mean(iv), 2) if iv else None,
                          "p95_ms": sorted(iv)[int(0.95 * (len(iv) - 1))] if iv else None,
                          "max_ms": max(iv) if iv else None, "skipped": sum(1 for x in iv if x > 150)},
        "distance_readings": len(dist), "distance_ok": len(dist_ok),
        "ml_results": len(ml), "ml_ok": len(ml_ok),
        "invoke_us_median": statistics.median(inv) if inv else None, "invoke_us_max": max(inv) if inv else None,
        "events_sent": sent, "events_processed": len(app), "occupancy_status": occ,
        "log_stored": stored, "log_not_stored": len(notst), "log_full": full, "log_queue_dropped": dropped,
        "log_write_us_max": max(logw) if logw else None, "log_status": status,
        "mbf_dropped": mbf_drops, "min_stack_free": min(stacks) if stacks else None,
        "heap_free": int(heap.group(1)) if heap else None, "heap_min_free": int(heap.group(2)) if heap else None,
        "resets": resets,
    }
    checks = [
        ("no reset", resets == 0),
        ("every event processed", len(app) == sent),
        ("every event offered to the log, none dropped", dropped == 0 and stored + len(notst) == sent),
        ("sampling mean within 5% of 100 ms", bool(iv) and abs(statistics.mean(iv) - 100) <= 5),
        ("model ran on live readings", len(ml_ok) > 0),
        ("no message buffer dropped a message", bool(mbf_drops) and not any(mbf_drops.values())),
    ]
    with open(a.log, "a") as log:
        log.write("# summary " + json.dumps(res) + "\n")
        for name, ok in checks:
            log.write(f"# {'PASS' if ok else 'FAIL'} {name}\n")
    print(json.dumps(res, indent=1))
    for name, ok in checks:
        print(f"{'PASS' if ok else 'FAIL'}  {name}")
    print(f"{sum(ok for _, ok in checks)}/{len(checks)} passed. Raw log: {a.log}")
    sys.exit(0 if all(ok for _, ok in checks) else 1)


if __name__ == "__main__":
    main()
