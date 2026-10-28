#!/usr/bin/env python3
"""Capture the D2 SPI loopback benchmark and produce raw, CSV, and summary evidence."""

import argparse
import csv
import json
import re
import statistics
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

import serial


SIZES = (8, 16, 32, 64, 128, 256, 512, 1024)
PREFIX = "[SPIBENCH] "


def fields(line):
    return {key: int(value) for key, value in re.findall(r"([a-z_]+)=(\d+)", line)}


def percentile(samples, percentage):
    ordered = sorted(samples)
    rank = (len(ordered) - 1) * percentage / 100
    lower = int(rank)
    fraction = rank - lower
    return ordered[lower] * (1 - fraction) + ordered[min(lower + 1, len(ordered) - 1)] * fraction


def capture(port, mode, hz, timeout, output, runtime):
    samples = {size: [] for size in SIZES}
    summaries = {}
    begin = None
    end = None
    deadline = time.monotonic() + timeout
    with serial.Serial(port, 115200, timeout=0.5, write_timeout=2) as device, output.open("w", encoding="utf-8") as raw:
        device.dtr = False
        device.rts = False
        if runtime == "arduino":
            device.reset_input_buffer()
            ready_deadline = time.monotonic() + 10
            next_ping = 0
            while time.monotonic() < ready_deadline:
                if time.monotonic() >= next_ping:
                    device.write(b"PING\r\n")
                    next_ping = time.monotonic() + 1
                line = device.readline().decode(errors="replace").strip()
                if line:
                    raw.write(line + "\n")
                    raw.flush()
                if line.startswith(PREFIX + "READY"):
                    break
            else:
                raise TimeoutError("Benchmark sketch did not announce READY")
            device.write(f"BENCH {mode} {hz}\r\n".encode())
        else:
            # Opening native USB can reset the ESP32-S3. Wait until the
            # µT-Kernel ControlTask has started before sending its command.
            time.sleep(3)
            device.write(f"SPIBENCH {mode} {hz}\r\n".encode())
        while time.monotonic() < deadline:
            line = device.readline().decode(errors="replace").strip()
            if not line:
                continue
            raw.write(line + "\n")
            raw.flush()
            if PREFIX not in line:
                continue
            message = line[line.index(PREFIX) + len(PREFIX):]
            if message.startswith("ERROR"):
                raise RuntimeError(message)
            if message.startswith("BEGIN "):
                begin = fields(message)
                print(f"Benchmark started on {port}: mode {mode}, {hz} Hz")
            elif message.startswith("SIZE "):
                data = fields(message)
                size = data["bytes"]
                if size not in samples or size in summaries:
                    raise ValueError(f"Unexpected or duplicate size: {size}")
                summaries[size] = data
                print(f"{size:4} bytes: {data['attempts']} transfers, "
                      f"{data['platform_errors']} HAL errors, "
                      f"{data['mismatch_transactions']} mismatches")
            elif message.startswith("SAMPLES "):
                header, values = message.split(" values=", 1)
                data = fields(header)
                size = data["bytes"]
                if size not in samples or data["start"] != len(samples[size]):
                    raise ValueError(f"Unexpected sample chunk for {size} bytes")
                samples[size].extend(int(value) for value in values.split(","))
            elif message.startswith("END "):
                end = fields(message)
                break
    if end is None:
        raise TimeoutError(f"Benchmark did not finish within {timeout} seconds")
    return begin, summaries, samples, end


def write_results(folder, begin, summaries, samples, end, port, runtime):
    if begin is None or begin["attempts"] != 1000 or begin["warmups"] < 10:
        raise ValueError("Missing or invalid benchmark header")
    if begin["sclk"] != 11 or begin["mosi"] != 12 or begin["miso"] != 13 or begin["cs"] != 10:
        raise ValueError("Unexpected SPI pin mapping")
    if set(summaries) != set(SIZES):
        raise ValueError("Missing benchmark sizes")

    rows = []
    with (folder / "samples.csv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(("bytes", "trial", "latency_us"))
        for size in SIZES:
            data = summaries[size]
            values = samples[size]
            if data["attempts"] != len(values) or len(values) != 1000:
                raise ValueError(f"Expected 1000 samples at {size} bytes, got {len(values)}")
            if sum(values) != data["total_us"] or max(values) != data["max_us"]:
                raise ValueError(f"Timing totals disagree at {size} bytes")
            for trial, latency in enumerate(values):
                writer.writerow((size, trial, latency))
            total = data["attempts"]
            row = {
                **data,
                "median_us": round(statistics.median(values), 2),
                "p95_us": round(percentile(values, 95), 2),
                "p99_us": round(percentile(values, 99), 2),
                "throughput_bytes_per_s": round(size * total * 1_000_000 / data["total_us"], 2),
                "transaction_error_rate": round(data["failed_transactions"] / total, 6),
                "byte_error_rate": round(data["mismatch_bytes"] / (size * total), 9),
            }
            rows.append(row)

    try:
        commit = subprocess.check_output(("git", "rev-parse", "HEAD"), text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        commit = "unavailable"
    metadata = {
        "date": datetime.now().astimezone().isoformat(),
        "port": port,
        "git_commit": commit,
        "board": "ESP32-S3 Super Mini / HW-747",
        "fixture": "GPIO12 MOSI to GPIO13 MISO; TFT disconnected",
        "backend": "µT-Kernel SPI2 register-level HAL" if runtime == "mtk" else "EdgeS3 SPIClass HAL",
        "begin": begin,
        "end": end,
        "results": rows,
    }
    (folder / "summary.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    passed = end["failed_sizes"] == 0 and end["close"] == 0 and all(
        row["warmup_errors"] == 0 and row["platform_errors"] == 0
        and row["mismatch_transactions"] == 0 for row in rows
    )
    lines = [
        "# D2 SPI loopback benchmark",
        "",
        f"- Date: {metadata['date']}",
        f"- HAL base commit: `{commit}` (check working-tree status for benchmark sketch/script changes)",
        f"- Board/port: {metadata['board']} / {port}",
        f"- Fixture: {metadata['fixture']}",
        f"- HAL: {metadata['backend']}",
        f"- Clock/mode: {begin['hz']} Hz / {begin['mode']}",
        f"- Attempts: {begin['attempts']} per size after {begin['warmups']} warm-ups",
        f"- Result: {'PASS' if passed else 'FAIL'}",
        "- Observed resets: 0 (a complete BEGIN-to-END capture; an interrupted run fails instead of producing a PASS report)",
        f"- Free heap: {begin['heap_before']} B before; {end['heap_after']} B after in the {'µT-Kernel firmware' if runtime == 'mtk' else 'dedicated benchmark sketch'}",
        "",
        "| Bytes | Median µs | p95 µs | p99 µs | Max µs | B/s | HAL errors | Timeouts | Mismatch transactions | Mismatch bytes | Transaction error rate | Byte error rate |",
        "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        lines.append(
            f"| {row['bytes']} | {row['median_us']} | {row['p95_us']} | "
            f"{row['p99_us']} | {row['max_us']} | {row['throughput_bytes_per_s']} | "
            f"{row['platform_errors']} | {row['timeouts']} | "
            f"{row['mismatch_transactions']} | {row['mismatch_bytes']} | "
            f"{row['transaction_error_rate']} | {row['byte_error_rate']} |"
        )
    lines += ["", "Throughput counts payload bytes in one direction and divides by the sum of measured HAL call times. The host CSV contains all 8,000 transaction latencies; serial output occurs after each size's timed loop.", ""]
    (folder / "RESULTS.md").write_text("\n".join(lines), encoding="utf-8")
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--mode", type=int, default=0)
    parser.add_argument("--hz", type=int, default=10_000_000)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--runtime", choices=("arduino", "mtk"), default="arduino")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    minimum_hz = 1_000_000 if args.runtime == "mtk" else 1000
    if args.mode not in range(4) or not minimum_hz <= args.hz <= 20_000_000:
        parser.error(f"mode must be 0–3 and clock must be {minimum_hz}–20 MHz")
    root = Path("parity/logs/mtk/d2-spi" if args.runtime == "mtk" else "parity/logs/d2-spi")
    folder = args.output or root / datetime.now().strftime("bench-%Y%m%d-%H%M%S")
    folder.mkdir(parents=True, exist_ok=False)
    try:
        begin, summaries, samples, end = capture(args.port, args.mode, args.hz, args.timeout, folder / "serial.log", args.runtime)
        passed = write_results(folder, begin, summaries, samples, end, args.port, args.runtime)
    except (OSError, ValueError, RuntimeError, TimeoutError) as exc:
        print(f"Benchmark failed: {exc}. Partial log: {folder / 'serial.log'}", file=sys.stderr)
        return 1
    print(f"{'PASS' if passed else 'FAIL'}: {folder / 'RESULTS.md'}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
