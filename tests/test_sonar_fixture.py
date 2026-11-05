"""Fixture selection must not drive the V2 I2C or retained LCD connections."""
import importlib.util
from pathlib import Path
import re
import unittest

path = Path(__file__).resolve().parents[1] / "parity/parity_test.py"
spec = importlib.util.spec_from_file_location("sonar_parity", path)
parity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parity)


class SonarFixtureTests(unittest.TestCase):
    def test_upstream_stages_unchanged(self):
        for board in parity.BOARD_PINS:
            for stage, steps in parity.STAGES.items():
                expected = [(key, description, parity.map_pins(command, parity.BOARD_PINS[board]), wait,
                             [parity.map_pins(p, parity.BOARD_PINS[board]) for p in patterns])
                            for key, description, command, wait, patterns in steps]
                self.assertEqual(parity.stage_steps(stage, board), expected)

    def test_v2_only_configures_sonar_pins(self):
        for stage in ("4c-sonic", "sonic-rule"):
            steps = parity.stage_steps(stage, "s3", "v2-sonar")
            self.assertEqual([s[2] for s in steps[:2]], ["STOP", "CLEAR"])
            configs = [s[2] for s in steps if s[2].startswith("BEGINCFG")]
            self.assertEqual(len(configs), 1)
            self.assertEqual(re.findall(r"gpio\.(trig|echo)=(\d+)", configs[0]),
                             [("trig", "4"), ("echo", "5")])
            self.assertNotRegex(configs[0], r"gpio\.pin=")

    def test_v2_rejects_other_stages_and_boards(self):
        for stage in parity.STAGES:
            if stage not in ("4c-sonic", "sonic-rule"):
                with self.assertRaises(ValueError):
                    parity.stage_steps(stage, "s3", "v2-sonar")
        with self.assertRaises(ValueError):
            parity.stage_steps("4c-sonic", "pico", "v2-sonar")

    def test_v2_keeps_distance_and_rule_assertions(self):
        for stage in ("4c-sonic", "sonic-rule"):
            steps = parity.stage_steps(stage, "s3", "v2-sonar")[2:]
            for original, selected in zip(parity.STAGES[stage], steps):
                self.assertEqual(original[0], selected[0])
                self.assertEqual(original[3:], selected[3:])

    def test_manual_probe_stays_on_v2_and_does_not_auto_run(self):
        preset = path.parents[1] / "config/d3_sonar_v2_once.cfg"
        commands = [line for line in preset.read_text().splitlines()
                    if line and not line.startswith("#")]
        self.assertEqual(commands[:2], ["STOP", "CLEAR"])
        self.assertEqual(commands[-1], "SHOW")
        self.assertNotIn("RUN", commands)
        self.assertEqual(len(commands), 4)
        self.assertIn("gpio.trig=4|gpio.echo=5|gpio.pull=down", commands[2])
        self.assertIn("timing.mode=one_shot", commands[2])
        self.assertTrue(all(len(line.encode("ascii")) < 512 for line in commands))


if __name__ == "__main__":
    unittest.main()
