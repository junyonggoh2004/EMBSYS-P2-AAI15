#!/usr/bin/env python3
"""D3 application smoke test on the micro T-Kernel ESP32-S3 firmware.

Clears volatile sensor/rule configuration and resets application occupancy.
Exercises synthetic events and an unverified one-shot rule on GPIO8 (no wire
required). Sends no SPI programming commands; application flash logging must
remain disabled for this fixture. This is plumbing evidence, not a
physical entrance, queue-overflow, timing-budget or D2 T-23 acceptance test.
"""

import argparse
from datetime import datetime
from pathlib import Path
import re
import subprocess
import sys
import time


class CheckFailure(RuntimeError):
    pass


class Console:
    def __init__(self, serial_port, log, clock=time.monotonic):
        self.serial = serial_port
        self.log = log
        self.clock = clock
        self.pending = b""
        self.monitor_runtime = False

    def exchange(self, command, patterns, seconds=5.0):
        """Require every reply pattern; retain partial USB reads and raw lines."""
        self.log.write(f"# sent: {command}\n")
        self.log.flush()
        self.serial.write((command + "\n").encode())
        wanted = [re.compile(pattern) for pattern in patterns]
        matched = [False] * len(wanted)
        lines = []
        deadline = self.clock() + seconds
        while self.clock() < deadline:
            self.pending += self.serial.read(512)
            while b"\n" in self.pending:
                raw, self.pending = self.pending.split(b"\n", 1)
                line = raw.decode(errors="replace").rstrip("\r")
                lines.append(line)
                self.log.write(line + "\n")
                if self.monitor_runtime and re.search(
                    r"^ESP-ROM:|^\[BOOT\]|Guru Meditation|panic'ed|watchdog no longer fed", line
                ):
                    self.log.flush()
                    raise CheckFailure(f"unexpected reset/runtime fault: {line}")
                for i, pattern in enumerate(wanted):
                    matched[i] |= pattern.search(line) is not None
            if all(matched):
                self.log.flush()
                return lines
        self.log.flush()
        missing = [p.pattern for p, found in zip(wanted, matched) if not found]
        raise CheckFailure(f"{command}: missing replies {missing}")


def application_reply(event_id, result, occupancy, reason=None):
    suffix = rf".*occupancy=-?\d+->{occupancy}\b"
    if reason:
        suffix += rf".*reason={re.escape(reason)}\b"
    return rf"^\[APP\] id={event_id}\b.*result={result}\b{suffix}"


def validate_tasks(lines):
    """Check structural invariants; report resources without inventing limits."""
    task = next((re.search(
        r"^\[TASK\] ApplicationTask\s+id=\d+\s+core=(\d+)\s+pri=(\d+)\s+"
        r"stack_free=(\d+) B ran_on_core0=(\d+) ran_on_core1=(\d+)", line
    ) for line in lines if line.startswith("[TASK] ApplicationTask")), None)
    if not task:
        raise CheckFailure("TASKS: missing or malformed ApplicationTask report")
    core, priority, stack_free, core0, core1 = map(int, task.groups())
    if core != 1 or priority != 23 or core0 != 0 or core1 == 0 or stack_free == 0:
        raise CheckFailure(f"TASKS: unexpected application placement/resources {task.group(0)}")
    sizes = next((re.search(r"^\[TASK\] app_message=(\d+) B app_buffer=(\d+) B", line)
                  for line in lines if line.startswith("[TASK] app_message=")), None)
    if not sizes:
        raise CheckFailure("TASKS: missing typed application message sizes")
    message, buffer = map(int, sizes.groups())
    if message < 84 or buffer != 8 * (4 + ((message + 3) & ~3)):
        raise CheckFailure(f"TASKS: incorrect typed message storage {sizes.group(0)}")
    return {"core": core, "priority": priority, "stack_free": stack_free,
            "ran_on_core0": core0, "ran_on_core1": core1,
            "message_bytes": message, "buffer_bytes": buffer}


def run_smoke(console):
    checks = []

    def step(name, command, *patterns):
        lines = console.exchange(command, patterns)
        checks.append(name)
        print(f"PASS  {name}")
        return lines

    step("stop", "STOP", r"^STOP$")
    step("clear", "CLEAR", r"^CLEARED$")
    step("reset", "APP RESET", r"^\[APP\] reset occupancy=0\b")
    console.monitor_runtime = True
    cases = [
        ("entry", 101, 1, "entry", 0, 1),
        ("duplicate", 101, 1, "entry", 2, 1),
        ("exit", 102, 2, "exit", 0, 0),
        ("exit-floor", 103, 3, "exit", 1, 0),
        ("incomplete", 104, 4, "incomplete", 1, 0),
        ("false", 105, 5, "false", 1, 0),
        ("reordered", 106, 4, "entry", 3, 0),
    ]
    for name, event_id, seq, klass, result, occupancy in cases:
        step(name, f"APP EVENT {event_id} {seq} {klass}",
             application_reply(event_id, result, occupancy))
    step("malformed-command", "APP EVENT 0 1 entry", r"^\[APP\] usage:")
    step("one-shot-config", "BEGINCFG|name=D3_PROBE|proto=gpio|gpio.mode=digital|"
         "gpio.pin=8|gpio.pull=down|mode=poll|timing.mode=one_shot|ENDCFG",
         r"^CFG: ok \(added #\d+: D3_PROBE\)")
    step("rule-config", "BEGINRULE|name=D3_UNVERIFIED|source=D3_PROBE|calc=s=u8(0)|"
         "when=1|action=event:entry|ENDRULE", r"^RULE: ok")
    step("unverified-rule-rejected", "RUN",
         r"^\[RULE\] D3_UNVERIFIED: event=entry id=\d+ observation=unverified queued=1",
         application_reply(r"\d+", 4, 0, "quality"))
    step("stop-probe", "STOP", r"^STOP$")
    step("status", "APP STATUS", r"^\[APP\] status occupancy=0\b")
    lines = step("task-report", "TASKS", r"^\[TASK\] app_message=",
                 r"^\[TASK\] ApplicationTask\s", r"^\[TASK\] heap_free=")
    resources = validate_tasks(lines)
    checks.append("task-placement-and-storage")
    print(f"PASS  task-placement-and-storage {resources}")
    return checks


def metadata():
    def git(*args):
        try:
            return subprocess.check_output(["git", *args], text=True,
                                           stderr=subprocess.DEVNULL).strip()
        except (OSError, subprocess.CalledProcessError):
            return "unavailable"
    return f"host_commit={git('rev-parse', 'HEAD')} dirty={bool(git('status', '--porcelain'))}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="ESP32-S3 USB serial port")
    parser.add_argument("--log", type=Path,
                        default=Path("parity/logs/mtk") /
                        f"d3-application-{datetime.now().strftime('%Y%m%d-%H%M%S')}.log")
    args = parser.parse_args()
    try:
        import serial
    except ImportError:
        parser.error("pyserial is missing; use the test environment installed by tools/mtk.sh setup")
    revision = metadata()  # before creating our own untracked evidence file
    args.log.parent.mkdir(parents=True, exist_ok=True)
    ok = True
    try:
        with args.log.open("x") as log, serial.Serial(args.port, 115200, timeout=0.1) as port:
            log.write(f"# {datetime.now().astimezone().isoformat()} {revision} port={args.port}\n")
            console = Console(port, log)
            try:
                checks = run_smoke(console)
                log.write(f"# application smoke: {len(checks)} checks passed\n")
            except CheckFailure as error:
                ok = False
                print(f"FAIL  {error}", file=sys.stderr)
                log.write(f"# FAIL {error}\n")
            finally:
                # This fixture leaves no active probe or synthetic occupancy.
                for command, reply in [("STOP", r"^STOP$"), ("CLEAR", r"^CLEARED$"),
                                       ("APP RESET", r"^\[APP\] reset occupancy=0\b")]:
                    try:
                        console.exchange(command, [reply])
                    except CheckFailure as error:
                        ok = False
                        print(f"FAIL  cleanup: {error}", file=sys.stderr)
                        log.write(f"# FAIL cleanup: {error}\n")
                log.write("# Scope: synthetic application plumbing only; physical entrance, "
                          "overflow, timing and D2 T-23 not certified\n")
                log.write(f"# Final outcome: {'PASS' if ok else 'FAIL'}\n")
    except (OSError, serial.SerialException) as error:
        print(f"FAIL  {error}", file=sys.stderr)
        return 1
    print(f"Application smoke {'PASS' if ok else 'FAIL'}. Raw log: {args.log}")
    print("Physical entrance, overflow, timing and D2 T-23 acceptance remain separate.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
