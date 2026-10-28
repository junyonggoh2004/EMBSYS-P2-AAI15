#!/usr/bin/env python3
"""mtk_live.py: run a real sensor on the board and show its readings decoded.

    sh tools/mtk.sh live sonic      HC-SR04: Trig GPIO1, Echo GPIO2 through a divider
    sh tools/mtk.sh live mag        GY-511: SDA GPIO1, SCL GPIO2

Configures the sensor with the same lines as the parity stages 4c-sonic and
4c-mag, starts sampling, prints every reading in units (cm, g, gauss, degrees)
next to the raw bytes the board sent, and stops after --seconds or Ctrl+C.
Everything the board prints is saved with timestamps to
parity/logs/mtk/live-<sensor>.log, so the run can be checked afterwards.
Works on either build (µT-Kernel or Arduino); it only uses the console.
"""
import argparse, math, re, sys, time
from pathlib import Path

import serial
from serial.tools import list_ports

CONFIGS = {
    "sonic": [
        "BEGINCFG|name=HCSR04|proto=gpio|gpio.mode=pulse|mode=poll|freq_hz=2|gpio.trig=1|gpio.echo=2"
        "|gpio.trig_us=10|gpio.pulse_timeout_us=25000|ENDCFG",
    ],
    "mag": [
        "BEGINCFG|name=MAGID|proto=i2c|mode=poll|freq_hz=1|i2c.sda=1|i2c.scl=2|i2c.addr=0x1E|i2c.reg=0x0A"
        "|i2c.reg_size=1|i2c.read_len=3|i2c.restart=1|ENDCFG",
        "BEGINCFG|name=ACC|proto=i2c|mode=poll|freq_hz=1|i2c.sda=1|i2c.scl=2|i2c.addr=0x19|i2c.pre=0x20 0x57"
        "|i2c.reg=0xA8|i2c.reg_size=1|i2c.read_len=6|i2c.restart=1|ENDCFG",
        "BEGINCFG|name=MAG|proto=i2c|mode=poll|freq_hz=1|i2c.sda=1|i2c.scl=2|i2c.addr=0x1E"
        "|i2c.pre=0x00 0x10 0x01 0xE0 0x02 0x00|i2c.reg=0x03|i2c.reg_size=1|i2c.read_len=6|i2c.restart=1|ENDCFG",
    ],
}
FRAME = re.compile(r"^proto=\w+ src=(\w+) len=(\d+) ts=(\d+) : ((?:[0-9A-F]{2} ?)+)$")
TIMEOUT_US = 25000


def s16le(lo, hi):
    v = lo | hi << 8
    return v - 65536 if v >= 32768 else v


def s16be(hi, lo):
    return s16le(lo, hi)


def decode(src, b):
    """Returns a readable description of one sensor frame."""
    if src == "HCSR04":
        if len(b) == 1 and b[0] == 0:
            return "no echo (nothing in range, or Echo not reaching GPIO2)"
        if len(b) == 3 and b[2] == 1:
            us = b[0] | b[1] << 8
            if us >= TIMEOUT_US:
                return f"{us} us = timeout: no echo ended within 25 ms"
            return f"echo {us} us = {us / 58.0:6.1f} cm"
    if src == "MAGID" and len(b) == 3:
        ident = bytes(b).decode(errors="replace")
        ok = "correct: an LSM303DLHC magnetometer" if ident == "H43" else "WRONG: expected H43"
        return f"identity '{ident}' ({ok})"
    if src == "ACC" and len(b) == 6:
        # LSM303DLHC accelerometer: X, Y, Z little-endian, 12 bits left-justified,
        # +-2 g range (CTRL_REG1 0x57: 100 Hz, all axes; 1 mg per count)
        x, y, z = (s16le(b[i], b[i + 1]) / 16 / 1000.0 for i in (0, 2, 4))
        return f"accel x {x:+.2f} y {y:+.2f} z {z:+.2f} g, total {math.sqrt(x*x + y*y + z*z):.2f} g (about 1.00 at rest)"
    if src == "MAG" and len(b) == 6:
        # LSM303DLHC magnetometer: X, Z, Y big-endian; gain set to +-8.1 gauss
        # (CRB_REG_M 0xE0): 230 counts per gauss on X and Y, 205 on Z
        x, zr, y = (s16be(b[i], b[i + 1]) for i in (0, 2, 4))
        gx, gy, gz = x / 230.0, y / 230.0, zr / 205.0
        heading = (math.degrees(math.atan2(gy, gx)) + 360.0) % 360.0
        return f"mag x {gx:+.2f} y {gy:+.2f} z {gz:+.2f} gauss, heading {heading:5.1f} deg if held flat"
    return "unrecognised frame"


def find_port():
    for p in list_ports.comports():
        if p.vid == 0x303A:
            return p.device
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sensor", choices=sorted(CONFIGS))
    ap.add_argument("--port", default=None)
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--log", default=None)
    args = ap.parse_args()
    port = args.port or find_port()
    if not port:
        sys.exit("No ESP32-S3 found on USB.")
    log_path = Path(args.log or f"parity/logs/mtk/live-{args.sensor}.log")
    log_path.parent.mkdir(parents=True, exist_ok=True)

    ser = serial.Serial(port, 115200, timeout=0.1)
    log = open(log_path, "w")
    log.write(f"# live {args.sensor} run, {time.strftime('%Y-%m-%d %H:%M:%S')}, {port}\n")
    buf = b""
    counts = {}

    def pump(seconds, show):
        nonlocal buf
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            buf += ser.read(512)
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode(errors="replace").rstrip("\r")
                log.write(f"{time.strftime('%H:%M:%S')} {line}\n")
                m = FRAME.match(line)
                if m and show:
                    src, data = m.group(1), [int(h, 16) for h in m.group(4).split()]
                    counts[src] = counts.get(src, 0) + 1
                    print(f"{time.strftime('%H:%M:%S')}  {src:6s} {m.group(4).strip():18s} {decode(src, data)}")
                elif show and (line.startswith("CFG:") or "FAILED" in line or "error" in line.lower()):
                    print(f"{time.strftime('%H:%M:%S')}  board: {line}")
        log.flush()

    def send(cmd, wait=1.0, show=False):
        log.write(f"{time.strftime('%H:%M:%S')} --- {cmd}\n")
        ser.write((cmd + "\r\n").encode())
        pump(wait, show)

    try:
        send("STOP"); send("CLEAR")
        print(f"Configuring {args.sensor} on {port}...")
        for line in CONFIGS[args.sensor]:
            send(line, 2.0, show=True)
        print(f"Running for {args.seconds:g} s (Ctrl+C stops early). Readings:")
        send("RUN", 0.1)
        pump(args.seconds, show=True)
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        send("STOP"); send("CLEAR")
        log.close()
        ser.close()
    print("Readings received: " + (", ".join(f"{k} {v}" for k, v in counts.items()) or "none"))
    print(f"Log saved to {log_path}")


if __name__ == "__main__":
    main()
