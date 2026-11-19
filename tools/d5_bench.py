"""D5: time the D4 models on the board (README section 6, tasks 4.2 to 4.5).

  bench    For each model: STOP, ML MODEL <id> (records the tensor arena it
           uses), then MLBENCH <runs> RAW (records every run and the
           summary, and whether ESP-NN's S3 vector kernel ran). Ends with
           TASKS. Needs no sensors. Run it once on the normal firmware and
           once on the NN=ansi firmware.
  live     Task 4.5: the ML pipeline already running with sensors and MQTT
           (ML PRESET, ML RUN, RUN typed beforehand); collects the invoke_us
           of the next <samples> ML results, with TASKS before and after.
  compare  Task 4.2: two bench results (S3 assembly, then plain C) side by
           side, with the speed-up of each model.

Each session saves its console log and results.json under
parity/logs/mtk/d5/<build>-<date>/. The statistics match MLBENCH's: median
(the integer mean of the two middle values for an even count),
nearest-rank 95th percentile, minimum, maximum and rounded mean.
"""
import argparse
import json
import re
import sys
import time
from pathlib import Path

MODELS = ["sine-float", "sine-int8", "autoencoder", "comfort-summer", "people-counter"]
LOG_ROOT = Path("parity/logs/mtk/d5")


# ---------- parsing (no board needed; covered by tests/test_d5_bench.py) ----------

def stats(values):
    """Same definitions as ml_bench_stats() in ml_bench.c."""
    v = sorted(int(x) for x in values)
    n = len(v)
    if not n:
        return None
    median = v[n // 2] if n % 2 else (v[n // 2 - 1] + v[n // 2]) // 2
    return {"median": median, "p95": v[(95 * n + 99) // 100 - 1], "min": v[0],
            "max": v[-1], "mean": (sum(v) + n // 2) // n}


def parse_arena(line):
    m = re.search(r"\[TFLITE\]\s+Arena:\s+(\d+) / (\d+) bytes used", line)
    return (int(m.group(1)), int(m.group(2))) if m else None


def parse_vector_calls(line):
    m = re.search(r"\[ML-ACCEL\] \S+ vector_calls=(\d+)", line)
    return int(m.group(1)) if m else None


def parse_bench_json(line):
    if not line.startswith('{"type":"mlbench"'):
        return None
    try:
        return json.loads(line)
    except ValueError:
        return None


def parse_run(line):
    m = re.match(r"\[MLBENCH\] run=(\d+) request_us=(\d+) invoke_us=(\d+)$", line)
    return tuple(int(g) for g in m.groups()) if m else None


def parse_memory(line):
    m = re.match(r"\[MLBENCH\] memory internal_free=(\d+) B internal_min_free=(\d+) B psram_free=(\d+) B", line)
    return dict(zip(("internal_free", "internal_min_free", "psram_free"), map(int, m.groups()))) if m else None


def parse_heap(line):
    m = re.match(r"\[TASK\] heap_free=(\d+) B min_free=(\d+) B largest=(\d+) B", line)
    return dict(zip(("heap_free", "min_free", "largest"), map(int, m.groups()))) if m else None


def parse_ml_invoke(line):
    """invoke_us of one successful ML pipeline result (task 4.5)."""
    if not line.startswith('{"type":"ml"'):
        return None
    try:
        record = json.loads(line)
    except ValueError:
        return None
    if record.get("status") != "OK" or not isinstance(record.get("invoke_us"), int):
        return None
    return record["invoke_us"]


def redact(line):
    line = re.sub(r"(ssid[=:]\s*)\S+", r"\1<ssid>", line, flags=re.I)
    return re.sub(r"\b([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}\b", "<mac>", line)


# ---------- talking to the board ----------

class Console:
    """Line I/O on the board's console, with every line logged."""

    def __init__(self, conn, log):
        self.conn, self.log = conn, log

    def send(self, text):
        self.log.append(f"> {text}")
        self.conn.write((text + "\r\n").encode("ascii"))
        self.conn.flush()

    def lines(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            raw = self.conn.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if line:
                self.log.append(redact(line))
                yield line

    def drain(self, seconds=0.5):
        for _ in self.lines(seconds):
            pass


def bench_model(console, model, runs):
    result = {"model": model}
    console.send(f"ML MODEL {model}")
    for line in console.lines(10):
        arena = parse_arena(line)
        if arena:
            result["arena_bytes"], result["arena_size"] = arena
        if line.startswith("ML: ERROR"):
            result["error"] = line
            return result
        if line == f"ML: OK MODEL {model}":
            break
    else:
        result["error"] = "no answer to ML MODEL"
        return result

    console.send(f"MLBENCH {runs} RAW")
    raw = []
    for line in console.lines(60):
        calls = parse_vector_calls(line)
        if calls is not None:
            result["vector_calls"] = calls
        run = parse_run(line)
        if run:
            raw.append(run)
        if line.startswith("[MLBENCH] ERROR") or "[MLBENCH] usage" in line:
            result["error"] = line
            return result
        summary = parse_bench_json(line)
        if summary:
            result.update({k: summary[k] for k in summary if k != "type"})
        memory = parse_memory(line)
        if memory:
            result["memory"] = memory
            break
    else:
        if "request_us" not in result:
            result["error"] = "no MLBENCH summary"
            return result
    result["raw"] = [{"run": r, "request_us": q, "invoke_us": i} for r, q, i in raw]
    if len(raw) != result.get("runs"):
        result["error"] = f"{len(raw)} raw runs for {result.get('runs')} requested"
    return result


def tasks_heap(console, other=None):
    """TASKS' internal heap line; other lines seen meanwhile go to other()."""
    console.send("TASKS")
    for line in console.lines(3):
        heap = parse_heap(line)
        if heap:
            return heap
        if other:
            other(line)
    return None


def bench_session(console, build, models, runs):
    console.send("STOP")
    console.drain(1.0)
    results = {"kind": "bench", "build": build, "runs": runs, "models": []}
    for model in models:
        r = bench_model(console, model, runs)
        results["models"].append(r)
        calls = r.get("vector_calls")
        if model.endswith("int8") and calls is not None:
            if build == "optimized" and calls == 0:
                r["warning"] = "S3 vector kernel not called on the optimized build"
            if build == "ansi" and calls > 0:
                r["warning"] = "S3 vector kernel called on the plain C build"
    results["tasks_heap"] = tasks_heap(console)
    return results


def live_session(console, samples):
    values = []

    def take(line):
        v = parse_ml_invoke(line)
        if v is not None and len(values) < samples:
            values.append(v)

    results = {"kind": "live", "heap_before": tasks_heap(console, take)}
    if len(values) < samples:
        for line in console.lines(max(60, samples * 5)):
            take(line)
            if len(values) == samples:
                break
    results["samples"] = len(values)
    results["invoke_us"] = stats(values)
    results["raw"] = values
    results["heap_after"] = tasks_heap(console)
    return results


# ---------- reporting ----------

def bench_table(results):
    rows = ["| Model | Arena (B) | S3 vector calls | Request median / p95 (µs) | Invoke() median / p95 (µs) | Overhead median (µs) | Output |",
            "|---|---|---|---|---|---|---|"]
    for r in results["models"]:
        if "error" in r and "request_us" not in r:
            rows.append(f"| {r['model']} | | | {r['error']} | | | |")
            continue
        rq, iv, ov = r["request_us"], r["invoke_us"], r["overhead_us"]
        rows.append(f"| {r['model']} | {r.get('arena_bytes', '')} | {r.get('vector_calls', '')} | "
                    f"{rq['median']} / {rq['p95']} | {iv['median']} / {iv['p95']} | {ov['median']} | {r.get('output0', '')} |")
    return "\n".join(rows)


def compare_table(optimized, ansi):
    by_model = {r["model"]: r for r in ansi["models"] if "invoke_us" in r}
    rows = ["| Model | Invoke() with S3 assembly (µs) | Invoke() plain C (µs) | Speed-up | Request with S3 assembly (µs) | Request plain C (µs) | Same output |",
            "|---|---|---|---|---|---|---|"]
    for r in optimized["models"]:
        a = by_model.get(r["model"])
        if "invoke_us" not in r or not a:
            continue
        fast, slow = r["invoke_us"]["median"], a["invoke_us"]["median"]
        speedup = f"{slow / fast:.2f}x" if fast else "n/a"
        same = "yes" if r.get("output0") == a.get("output0") else "no"
        rows.append(f"| {r['model']} | {fast} | {slow} | {speedup} | {r['request_us']['median']} | "
                    f"{a['request_us']['median']} | {same} |")
    return "\n".join(rows)


def save(results, log, build):
    out = LOG_ROOT / f"{build}-{time.strftime('%Y%m%d-%H%M')}"
    out.mkdir(parents=True, exist_ok=True)
    (out / "console.log").write_text("\n".join(log) + "\n")
    (out / "results.json").write_text(json.dumps(results, indent=1) + "\n")
    return out


def open_port(port):
    import serial  # only needed with a board
    return serial.Serial(port, 115200, timeout=0.1, write_timeout=2)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("bench")
    b.add_argument("--port", required=True)
    b.add_argument("--build", required=True, choices=["optimized", "ansi"],
                   help="optimized: the normal firmware; ansi: built with NN=ansi")
    b.add_argument("--runs", type=int, default=200)
    b.add_argument("--models", nargs="+", default=MODELS)
    lv = sub.add_parser("live")
    lv.add_argument("--port", required=True)
    lv.add_argument("--samples", type=int, default=100)
    c = sub.add_parser("compare")
    c.add_argument("optimized")
    c.add_argument("ansi")
    args = ap.parse_args(argv)

    if args.cmd == "compare":
        opt = json.loads(Path(args.optimized).read_text())
        ansi = json.loads(Path(args.ansi).read_text())
        print(compare_table(opt, ansi))
        return 0

    log = []
    console = Console(open_port(args.port), log)
    if args.cmd == "bench":
        results = bench_session(console, args.build, args.models, args.runs)
        out = save(results, log, args.build)
        print(bench_table(results))
    else:
        results = live_session(console, args.samples)
        out = save(results, log, "live")
        print(json.dumps({k: results[k] for k in ("samples", "invoke_us", "heap_before", "heap_after")}))
    for r in results.get("models", []):
        for key in ("error", "warning"):
            if key in r:
                print(f"{r['model']}: {r[key]}")
    print(f"Saved {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
