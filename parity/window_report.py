#!/usr/bin/env python3
"""Record D3 window metrics after the operator has stopped a trial.

Sends only WINDOW STATUS with a fresh nonce. Never configures sensors, RUNs,
STOPs, resets occupancy or writes flash. Snapshots are observations, not a
physical-accuracy, crosstalk, resource or full-workload acceptance certificate.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import secrets
import sys

try:
    from .application_test import CheckFailure, Console, metadata
except ImportError:
    from application_test import CheckFailure, Console, metadata

FIELDS = {
    "state": "version configured enabled state events dropped radar_frames malformed rx_dropped rx_dropped_start identity event_schema now_us".split(),
    "timing": "pairs valid failed cancelled deadline_misses skipped min_us mean_us max_us p95_le_us p99_le_us".split(),
    "events": "entry exit incomplete false_events window_deadlines radar_stale window_faults".split(),
    "config": "period_us window_us pair_limit_us clear_us radar_stale_us max_pairs guard_us timeout_a_us timeout_b_us occupied_a_mm occupied_b_mm".split(),
    "geometry": "a_name a_trig a_echo b_name b_trig b_echo radar_name radar_tx radar_rx xmin xmax ymin ymax".split(),
    "end": [],
}
NAMES = {"a_name", "b_name", "radar_name"}
BOUNDS = {"p95_le_us", "p99_le_us"}
LINE = re.compile(r"^\[WINDOW_REPORT\] token=([0-9a-fA-F]{8}) kind=(\w+)(?: (.*))?$")


def parse_report(lines, token):
    if not re.fullmatch(r"[0-9a-fA-F]{8}", token):
        raise CheckFailure("report nonce must be eight hexadecimal digits")
    sections = {}
    for line in lines:
        if re.search(r"^ESP-ROM:|^\[BOOT\]|Guru Meditation|panic'ed|watchdog no longer fed", line):
            raise CheckFailure("runtime reset/fault during snapshot")
        match = LINE.fullmatch(line)
        if not match or match[1].lower() != token.lower():
            continue
        kind = match[2]
        if kind == "error":
            raise CheckFailure(f"firmware refused snapshot: {match[3]}; STOP sampling and finish PAIR first")
        if kind not in FIELDS or kind in sections:
            raise CheckFailure(f"unexpected or duplicate report section: {kind}")
        values = {}
        for field in (match[3] or "").split():
            if "=" not in field:
                raise CheckFailure("malformed report field")
            key, value = field.split("=", 1)
            if key in values:
                raise CheckFailure(f"duplicate report field: {key}")
            values[key] = value
        if set(values) != set(FIELDS[kind]):
            raise CheckFailure(f"missing/unexpected fields in {kind}")
        for key, value in list(values.items()):
            if key in NAMES or key in BOUNDS:
                continue
            if not re.fullmatch(r"-?\d+", value) or (kind != "geometry" and int(value) < 0):
                raise CheckFailure(f"invalid integer {kind}.{key}")
            values[key] = int(value)
        sections[kind] = values
    if set(sections) != set(FIELDS):
        raise CheckFailure(f"incomplete snapshot: missing {sorted(set(FIELDS)-set(sections))}")
    state, timing, events, config = (sections[k] for k in ("state", "timing", "events", "config"))
    if state["version"] != 1:
        raise CheckFailure("unsupported window report version")
    if any(state[k] not in (0, 1) for k in ("configured", "enabled", "identity")):
        raise CheckFailure("invalid report flags")
    if state["state"] not in (0, 1, 2) or (not state["configured"] and timing["pairs"]):
        raise CheckFailure("invalid controller state")
    if timing["pairs"] != timing["valid"] + timing["failed"] + timing["cancelled"]:
        raise CheckFailure("pair outcome counters do not balance")
    if timing["deadline_misses"] > timing["pairs"]:
        raise CheckFailure("more deadline misses than attempts")
    if state["events"] != sum(events[k] for k in ("entry", "exit", "incomplete", "false_events")):
        raise CheckFailure("window event counters do not balance")
    if state["dropped"] > state["events"] or events["window_faults"] > state["events"]:
        raise CheckFailure("invalid window fault/drop counters")
    n, maximum = timing["pairs"], timing["max_us"]
    if not timing["min_us"] <= timing["mean_us"] <= maximum:
        raise CheckFailure("invalid min/mean/max timing")
    if not n and any(timing[k] for k in ("min_us", "mean_us", "max_us")):
        raise CheckFailure("durations exist without attempts")
    ranks = []
    for key in ("p95_le_us", "p99_le_us"):
        raw = timing[key]
        overflow = raw == ">50000"
        if not overflow and not re.fullmatch(r"\d+", raw):
            raise CheckFailure("invalid percentile bound")
        bound = None if overflow else int(raw)
        if overflow and (not n or maximum <= 50000):
            raise CheckFailure("percentile overflow without an over-budget sample")
        if bound is not None:
            ceiling = max(1000, ((maximum + 999) // 1000) * 1000)
            if (not n and bound) or (n and (bound < max(1000, timing["min_us"]) or bound % 1000 or bound > min(50000, ceiling))):
                raise CheckFailure("inconsistent percentile bound")
        ranks.append(float("inf") if overflow else bound)
        timing[key] = bound if n else None
        timing[key.replace("_le_us", "_overflow")] = overflow
    if ranks[0] > ranks[1]:
        raise CheckFailure("p95 exceeds p99")
    if state["configured"]:
        if config["pair_limit_us"] != 50000 or config["period_us"] != 100000:
            raise CheckFailure("unexpected D3 timing contract")
        if config["timeout_a_us"] + config["guard_us"] + config["timeout_b_us"] > config["pair_limit_us"]:
            raise CheckFailure("configured pair budget exceeds limit")
    sections.pop("end")
    sections["uart_loss_since_configure"] = (state["rx_dropped"] - state["rx_dropped_start"]) & 0xffffffff
    sections["pair_budget_exceeded"] = None if not n else bool(timing["deadline_misses"] or maximum > config["pair_limit_us"])
    sections["acceptance"] = "not_evaluated"
    sections["percentile_resolution_us"] = 1000
    sections["sample_scope"] = "all_attempts_including_cooldown_failures_and_cancellations"
    return sections


def collect(console, token):
    console.monitor_runtime = True
    lines = console.exchange(f"WINDOW STATUS {token}",
                             [rf"^\[WINDOW_REPORT\] token={re.escape(token)} kind=end$"])
    return parse_report(lines, token)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log-dir", type=Path, default=Path("parity/logs/mtk/d3"))
    parser.add_argument("--note", default="", help="fixture/trial note saved with the snapshot")
    args = parser.parse_args(argv)
    try:
        import serial
    except ImportError:
        parser.error("pyserial is missing; use tools/mtk.sh setup's test environment")
    created = datetime.now(timezone.utc)
    token = secrets.token_hex(4)
    prefix = args.log_dir / f"d3-window-{created.strftime('%Y%m%dT%H%M%S%fZ')}-{token}"
    raw_path, json_path = prefix.with_suffix(".log"), prefix.with_suffix(".json")
    host = metadata()  # before creating evidence files; this is not board provenance
    try:
        args.log_dir.mkdir(parents=True, exist_ok=True)
        with raw_path.open("x") as log:
            log.write(f"# {created.isoformat()} {host} firmware_revision=unverified port={args.port}\n")
            port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
            # Set control lines before opening; never request a reset/boot mode.
            port.dtr = False
            port.rts = False
            port.port = args.port
            try:
                port.open()
                snapshot = collect(Console(port, log), token)
            except (CheckFailure, OSError, serial.SerialException) as error:
                log.write(f"# snapshot failed: {error}\n")
                raise
            finally:
                port.close()
        snapshot["metadata"] = {"created_utc": created.isoformat(), "host": host,
                                "firmware_revision": "unverified", "port": args.port,
                                "note": args.note, "token": token}
        with json_path.open("x") as output:
            json.dump(snapshot, output, indent=2)
            output.write("\n")
    except (CheckFailure, OSError, serial.SerialException) as error:
        print(f"Snapshot failed: {error}. Raw log: {raw_path}", file=sys.stderr)
        return 1
    print(f"Snapshot recorded: {json_path}\nRaw log: {raw_path}")
    print("Physical acceptance was not evaluated; retain matching build/image and fixture evidence.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
