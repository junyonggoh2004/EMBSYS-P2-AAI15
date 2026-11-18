import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("d6_demo", ROOT / "tools/d6_demo.py")
demo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(demo)


def options(**overrides):
    values = dict(node_a="input-a", node_b="processor-b", node_c="output-c",
                  broker="127.0.0.1", stage="transport", fixture="v2-i2c",
                  inputs="aht-light", light_address="0x29", style="complete")
    return argparse.Namespace(**dict(values, **overrides))


class DemoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.probe = Path(cls.temp.name) / "config-probe"
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-I", str(ROOT / "libraries/EdgeCore/src"),
                       str(ROOT / "tests/d6_config_probe.c"),
                       str(ROOT / "libraries/EdgeCore/src/application/config_parser.c"),
                       str(ROOT / "libraries/EdgeCore/src/application/timing_contract.c"),
                       "-o", str(cls.probe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def parsed(self, plan):
        blocks = []
        for action in plan["actions"]:
            if action["topic"].endswith("/config"):
                self.assertLess(len(action["payload"].encode()), 2048)
                for block in action["payload"].splitlines():
                    blocks.append(block.replace("|", "\n"))
        result = subprocess.run([str(self.probe)], input="\n".join(blocks) + "\n",
                                capture_output=True, text=True, check=True)
        return result.stdout

    def test_complete_and_piecewise_parse_to_same_board_state(self):
        complete = demo.make_plan(options())
        piecewise = demo.make_plan(options(style="piecewise"))
        self.assertEqual(self.parsed(complete), self.parsed(piecewise))
        self.assertEqual(sum(a["topic"].endswith("/config") for a in complete["actions"]), 3)
        self.assertEqual(sum(a["topic"].endswith("/config") for a in piecewise["actions"]), 6)
        for action in complete["actions"]:
            if action["topic"].endswith("/config"):
                if action["board"] == "A":
                    self.assertNotIn("proto=mqtt", action["payload"])
                    self.assertNotIn("proto=gpio", action["payload"])
                elif action["board"] == "B":
                    self.assertIn("remote_node=input-a", action["payload"])
                else:
                    self.assertIn("remote_node=processor-b", action["payload"])
                    self.assertNotIn("remote_node=input-a", action["payload"])
        runs = [a["board"] for a in complete["actions"] if a["payload"] == "RUN"]
        self.assertEqual(runs, ["C", "B", "A"])
        self.assertEqual(complete["actions"][0]["payload"], "STOP")
        self.assertEqual(complete["actions"][0]["board"], "A")

    def test_aht_only_excludes_all_light_mappings(self):
        plan = demo.make_plan(options(inputs="aht"))
        self.assertNotIn("LIGHT", json.dumps(plan))
        self.assertEqual(len(self.parsed(plan).splitlines()), 3)

    def test_identity_and_fixture_mismatch_rejected(self):
        with self.assertRaises(ValueError):
            demo.make_plan(options(node_c="input-a"))
        with self.assertRaises(ValueError):
            demo.make_plan(options(fixture="lcd-only"))
        for value in ["", "x/y", "*", "+", "x" * 32, "a\nb"]:
            with self.assertRaises(argparse.ArgumentTypeError):
                demo.node_id(value)

    def test_light_address_reaches_real_parser(self):
        for address in ["0x39", "0x49"]:
            plan = demo.make_plan(options(light_address=address))
            a = next(a for a in plan["actions"] if a["board"] == "A" and a["topic"].endswith("/config"))
            self.assertIn("i2c.addr=" + address, a["payload"])
            self.assertEqual(len(self.parsed(plan).splitlines()), 6)

    def test_lcd_is_separate_and_shell_is_parseable(self):
        plan = demo.make_plan(options(stage="lcd-pattern", fixture="lcd-only"))
        self.assertEqual([a["board"] for a in plan["actions"]], ["C", "C"])
        self.assertEqual(plan["actions"][0]["payload"], "LCDTEST 76 284 82 18 00 0")
        self.assertFalse(any(a["topic"].endswith("/config") for a in plan["actions"]))
        for stage in [plan, demo.make_plan(options(broker="host'; touch /tmp/should-not-run; '"))]:
            shell = demo.shell_plan(stage)
            subprocess.run(["sh", "-n"], input=shell, text=True, check=True)
            self.assertIn("[ -t 0 ]", shell)
            self.assertNotIn("-r -", shell)  # no retained configuration publishing

    def test_noninteractive_shell_exits_before_publishing(self):
        with tempfile.TemporaryDirectory() as tmp:
            publisher = Path(tmp) / "mosquitto_pub"
            publisher.write_text("#!/bin/sh\necho UNEXPECTED_PUBLISH; exit 99\n")
            publisher.chmod(0o755)
            env = dict(os.environ, PATH=tmp + os.pathsep + os.environ["PATH"])
            shell = demo.shell_plan(demo.make_plan(options()))
            result = subprocess.run(["sh"], input=shell, capture_output=True,
                                    text=True, env=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Run interactively", result.stderr)
            self.assertNotIn("UNEXPECTED_PUBLISH", result.stdout)


if __name__ == "__main__":
    unittest.main()
