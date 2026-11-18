#!/usr/bin/env python3
"""Render D6 MQTT demo commands. Never connects, flashes or writes hardware.

Default output is a JSON action plan. --format shell emits an interactive
script: run it separately to publish, checking the stated evidence at each
step. Node IDs must match the firmware actually running on the boards.
"""
import argparse
import json
from pathlib import Path
import re
import shlex

ROOT = Path(__file__).resolve().parents[1]


def node_id(value):
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,31}", value):
        raise argparse.ArgumentTypeError("node ID must be 1–31 ASCII letters, digits, _ or -")
    return value


def configs(board, nodes, inputs, light_address):
    path = ROOT / "config" / "d6" / f"board_{board.lower()}.cfg.in"
    text = path.read_text().replace("@NODE_A@", nodes["A"]).replace("@NODE_B@", nodes["B"])
    text = text.replace("@LIGHT_ADDRESS@", light_address)
    blocks = [line.strip() for line in text.splitlines()
              if line.strip() and not line.lstrip().startswith("#")]
    if inputs == "aht":
        blocks = [line for line in blocks if "LIGHT" not in line]
    if any("@" in line or len(line) >= 512 for line in blocks):
        raise ValueError("unresolved/oversized configuration block")
    return blocks


def make_plan(args):
    nodes = dict(zip("ABC", (args.node_a, args.node_b, args.node_c)))
    if len(set(nodes.values())) != 3:
        raise ValueError("A, B and C require distinct node IDs")
    expected_fixture = "v2-i2c" if args.stage == "transport" else "lcd-only"
    if args.fixture != expected_fixture:
        raise ValueError(f"{args.stage} requires --fixture {expected_fixture}")
    actions = []

    def add(board, channel, payload, expect):
        actions.append({"board": board, "topic": f"pico/{nodes[board]}/{channel}",
                        "payload": payload, "expect": expect})

    if args.stage == "lcd-pattern":
        add("C", "cmd", "LCDTEST 76 284 82 18 00 0",
            "C serial: [LCD] test complete result=0 AND visible correct bands/text/edges; capture a photo. FAIL on a nonzero result.")
        add("C", "cmd", "TASKS", "Capture C task placement, stack/heap and drop counters.")
    else:
        # Quiesce the physical producer before replacing receiver mappings.
        for board in "ABC":
            add(board, "cmd", "STOP", f"{board} serial: [CMD] STOP")
            add(board, "cmd", "ML STOP", f"{board} serial: ML: OK STOP (transport checkpoint has no inference)")
        for board in "CBA":
            add(board, "cmd", "CLEAR", f"{board} serial: [CMD] CLEAR (scheduler + rules + bridge)")
            blocks = configs(board, nodes, args.inputs, args.light_address)
            groups = [blocks] if args.style == "complete" else [[block] for block in blocks]
            for group in groups:
                names = [re.search(r"\|name=([^|]+)", block).group(1) for block in group]
                add(board, "config", "\n".join(group),
                    f"pico/{nodes[board]}/status/config: " + ", ".join(f"OK:{name}" for name in names) +
                    "; reject FAIL/missing ACK. A config ACK alone does not prove subscription or data delivery.")
            add(board, "cmd", "SHOW", f"{board} serial: expected configurations exactly once, correct source/pins.")
        for board in "CBA":
            add(board, "cmd", "RUN", f"{board} serial: [CMD] RUN; remote callbacks are not gated by RUN/STOP.")
        for board in "ABC":
            add(board, "cmd", "TASKS", f"Capture {board} task placement, stack/heap and drop counters.")
    return {"stage": args.stage, "fixture": args.fixture, "style": args.style,
            "inputs": args.inputs, "nodes": nodes, "broker": args.broker,
            "status": "GENERATED; NOT RUN", "actions": actions}


def shell_plan(plan):
    # Build arguments with shell quoting; broker/node input is never shell code.
    lines = ["#!/bin/sh", "set -eu",
             "# Generated D6 plan. Requires mosquitto_pub, serial monitors and a subscriber.",
             "# Applying transport replaces volatile sensors/rules/bridge mappings via STOP/CLEAR.",
             "# Type ok only after checking each stated condition; any other response stops.",
             "[ -t 0 ] || { echo 'Run interactively to verify acknowledgements.' >&2; exit 1; }",
             "verify_step() {",
             "  printf '%s\\n' \"$1\"",
             "  printf 'Observed as expected? Type ok: '",
             "  IFS= read -r answer",
             "  [ \"$answer\" = ok ] || { echo 'Stopped; restore the reviewed preset if needed.' >&2; exit 1; }",
             "}"]
    for i, action in enumerate(plan["actions"], 1):
        lines.append(f"# Step {i}: board {action['board']}")
        lines.append(shlex.join(["mosquitto_pub", "-h", plan["broker"], "-q", "0",
                                "-t", action["topic"], "-m", action["payload"]]))
        lines.append("verify_step " + shlex.quote(action["expect"]))
    lines.append("printf '%s\\n' 'Commands checked; data correlation and physical trial verdict remain manual. No D6 PASS is inferred.'")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--node-a", type=node_id, required=True)
    parser.add_argument("--node-b", type=node_id, required=True)
    parser.add_argument("--node-c", type=node_id, required=True)
    parser.add_argument("--broker", required=True, help="actual broker host; no credentials")
    parser.add_argument("--stage", choices=["transport", "lcd-pattern"], default="transport")
    parser.add_argument("--fixture", choices=["v2-i2c", "lcd-only"], required=True)
    parser.add_argument("--inputs", choices=["aht", "aht-light"], default="aht-light")
    parser.add_argument("--light-address", choices=["0x29", "0x39", "0x49"], default="0x29")
    parser.add_argument("--style", choices=["complete", "piecewise"], default="complete")
    parser.add_argument("--format", choices=["json", "shell"], default="json")
    args = parser.parse_args()
    if not args.broker or args.broker.startswith("-") or any(ch.isspace() or ord(ch) < 32 for ch in args.broker):
        parser.error("broker must be a hostname/IP without spaces or credentials")
    if any(ch in args.broker for ch in "@/\\"):
        parser.error("use a broker hostname/IP, not a URL or credentials")
    try:
        plan = make_plan(args)
    except ValueError as exc:
        parser.error(str(exc))
    print(shell_plan(plan) if args.format == "shell" else json.dumps(plan, indent=2), end="\n")


if __name__ == "__main__":
    main()
