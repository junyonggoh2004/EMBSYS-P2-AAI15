#!/usr/bin/env python3
"""D2 SPI REPL, validation, lifecycle, and MOSI-to-MISO loopback checks."""

import argparse
import re
import sys
import time
from pathlib import Path

import serial


def read_for(ser, seconds, log):
    data = b""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        data += ser.read(4096)
    text = data.decode(errors="replace").replace("\r", "")
    log.write(text)
    log.flush()
    return text


def command(ser, text, seconds, log):
    log.write(f"\n--- {text}\n")
    ser.write((text + "\r\n").encode())
    return read_for(ser, seconds, log)


def config(name, **overrides):
    values = {
        "proto": "spi", "mode": "poll", "freq_hz": "2",
        "spi.host": "fspi", "spi.sclk": "11", "spi.mosi": "12",
        "spi.miso": "13", "spi.cs": "10", "spi.hz": "10000000",
        "spi.mode": "0", "spi.tx": "0xA5 0x5A", "spi.read_len": "2",
        "spi.timeout_ms": "100",
    }
    values.update(overrides)
    fields = [f"name={name}"] + [f"{key}={value}" for key, value in values.items()]
    return "BEGINCFG|" + "|".join(fields) + "|ENDCFG"


def heap_value(text):
    match = re.search(r"heap_free=(\d+)", text)
    return int(match.group(1)) if match else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", default="parity/logs/d2-spi/spi.log")
    args = parser.parse_args()

    log_path = Path(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(name, passed, evidence=""):
        checks.append((name, passed, evidence))

    with log_path.open("w", encoding="utf-8") as log:
        ser = serial.Serial(args.port, 115200, timeout=0.1)
        ser.dtr = False
        ser.rts = False
        read_for(ser, 1.0, log)

        out = command(ser, "CLEAR", 1.0, log)
        check("clear", "CLEARED" in out, out)

        out = command(ser, config("SPI_LOOP"), 2.0, log)
        check("configure", "CFG: ok" in out and "SPI_LOOP" in out, out)

        out = command(ser, "SHOW", 1.5, log)
        check("show", "proto=spi" in out and
              "SPI host=fspi sclk=11 mosi=12 miso=13 cs=10 hz=10000000 mode=0 tx=2 rx=2 timeout_ms=100" in out, out)

        out = command(ser, "RUN", 4.0, log)
        check("loopback-2", bool(re.search(r"proto=spi src=SPI_LOOP len=2 ts=\d+ : A5 5A", out)), out)
        command(ser, "STOP", 1.0, log)
        command(ser, "CLEAR", 1.0, log)

        # Exercise every CPOL/CPHA mode with a distinct deterministic pattern.
        for spi_mode in range(1, 4):
            tx = f"0x{spi_mode:02X} 0x{(0xF0 | spi_mode):02X}"
            out_cfg = command(ser, config(f"MODE{spi_mode}", **{
                "spi.mode": str(spi_mode), "spi.tx": tx}), 1.0, log)
            out_run = command(ser, "RUN", 2.0, log)
            expected = rf"src=MODE{spi_mode} len=2 ts=\d+ : {spi_mode:02X} {0xF0 | spi_mode:02X}"
            check(f"mode-{spi_mode}", "CFG: ok" in out_cfg and bool(re.search(expected, out_run)), out_cfg + out_run)
            command(ser, "STOP", 0.5, log)
            command(ser, "CLEAR", 0.5, log)

        # The normal scheduler is deliberately capped at 128 RX bytes. Send
        # 64 deterministic bytes followed by 64 clocked zero-fill bytes.
        tx64 = " ".join(str(i) for i in range(64))
        out_cfg = command(ser, config("SPI_128", **{
            "spi.tx": tx64, "spi.read_len": "128", "freq_hz": "1"}), 1.5, log)
        out_run = command(ser, "RUN", 2.5, log)
        match = re.search(r"proto=spi src=SPI_128 len=128 ts=\d+ : ((?:[0-9A-F]{2} ?){128})", out_run)
        got = [int(x, 16) for x in match.group(1).split()] if match else []
        check("loopback-128", "CFG: ok" in out_cfg and got == list(range(64)) + [0] * 64,
              out_cfg + out_run)
        command(ser, "STOP", 0.5, log)
        command(ser, "CLEAR", 0.5, log)

        negative = [
            ("bad-host", {"spi.host": "hspi"}, "unsupported spi.host"),
            ("stream", {"mode": "stream"}, "spi stream unsupported"),
            ("bad-mode", {"spi.mode": "4"}, "spi.mode must be 0..3"),
            ("reserved-pin", {"spi.sclk": "19"}, "invalid or reserved SPI pin"),
            ("duplicate-pin", {"spi.cs": "11"}, "SPI pins must be unique"),
            ("missing-miso", {"spi.miso": "-1"}, "spi.miso required for RX"),
            ("oversize-rx", {"spi.read_len": "129"}, "spi.read_len exceeds 128"),
            ("zero-timeout", {"spi.timeout_ms": "0"}, "spi.timeout_ms out of range"),
            ("empty", {"spi.tx": "", "spi.read_len": "0"}, "SPI transfer is empty"),
        ]
        for name, override, expected in negative:
            out = command(ser, config("BAD_" + name, **override), 1.0, log)
            check(name, expected in out and "CFG: add failed (-5)" in out, out)

        # Different devices may share one host. Reusing the host with different
        # data pins must fail without disturbing the first device.
        out1 = command(ser, config("SHARE1", **{"spi.cs": "10"}), 1.0, log)
        out2 = command(ser, config("SHARE2", **{"spi.cs": "9", "spi.tx": "0x12 0x34"}), 1.0, log)
        bad = command(ser, config("CONFLICT", **{"spi.sclk": "14", "spi.cs": "8"}), 1.0, log)
        check("shared-host", "CFG: ok" in out1 and "CFG: ok" in out2, out1 + out2)
        check("host-conflict", "init_cfg FAILED for CONFLICT" in bad and "CFG: add failed (-2)" in bad, bad)
        out = command(ser, "RUN", 3.0, log)
        check("shared-survives", "src=SHARE1 len=2" in out and "A5 5A" in out and
              "src=SHARE2 len=2" in out and "12 34" in out, out)
        command(ser, "STOP", 1.0, log)
        command(ser, "CLEAR", 1.0, log)

        heap_before_text = command(ser, "TASKS", 1.0, log)
        heap_before = heap_value(heap_before_text)
        lifecycle_ok = True
        for cycle in range(100):
            opened = command(ser, config(f"LIFE{cycle}"), 0.12, log)
            cleared = command(ser, "CLEAR", 0.12, log)
            lifecycle_ok &= "CFG: ok" in opened and "CLEARED" in cleared
        heap_after_text = command(ser, "TASKS", 1.0, log)
        heap_after = heap_value(heap_after_text)
        heap_ok = (heap_before is not None and heap_after is not None and
                   heap_after >= heap_before - 512)
        check("lifecycle-100", lifecycle_ok, f"heap {heap_before} -> {heap_after}")
        check("heap-stable", heap_ok, f"heap {heap_before} -> {heap_after}")
        ser.close()

    for name, passed, evidence in checks:
        print(f"{'PASS' if passed else 'FAIL'}  {name}")
        if not passed:
            lines = [line for line in evidence.splitlines() if line.strip()]
            print("      " + (lines[-1] if lines else "no serial response"))
    passed = sum(ok for _, ok, _ in checks)
    print(f"\n{passed}/{len(checks)} passed. Raw log: {log_path}")
    return 0 if passed == len(checks) else 1


if __name__ == "__main__":
    sys.exit(main())
