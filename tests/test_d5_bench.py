"""D5 runner (tools/d5_bench.py) against a simulated board console; no board."""
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import d5_bench  # noqa: E402


class FakeBoard:
    """Answers the commands the runner sends, as the firmware prints them."""

    def __init__(self, vector_calls=2, fail_model=None, runs_short=0):
        self.pending, self.sent = [], []
        self.vector_calls, self.fail_model, self.runs_short = vector_calls, fail_model, runs_short
        self.model = None
        self.after_tasks = []      # lines the board prints after the next TASKS

    def write(self, data):
        cmd = data.decode().strip()
        self.sent.append(cmd)
        out = []
        if cmd.startswith("ML MODEL "):
            self.model = cmd[9:]
            if self.model == self.fail_model:
                out = ["ML: ERROR model load failed; pipeline disabled"]
            else:
                out = ["[TFLITE] Model loaded OK!", "[TFLITE]   Arena:   1234 / 65536 bytes used",
                       f"ML: OK MODEL {self.model}"]
        elif cmd.startswith("MLBENCH "):
            runs = int(cmd.split()[1])
            calls = self.vector_calls if self.model.endswith("int8") else 0
            out = [f"[ML-ACCEL] esp_nn_fc_s16_esp32s3 vector_calls={calls}"]
            out += [f"[MLBENCH] run={i} request_us={400 + i % 10} invoke_us={100 + i % 10}"
                    for i in range(1, runs + 1 - self.runs_short)]
            s = '{"median":%d,"p95":%d,"min":%d,"max":%d,"mean":%d}'
            out += [f"[MLBENCH] runs={runs} warmup=5 inputs=1 outputs=1 input=0.5 output0=0.25",
                    '{"type":"mlbench","runs":%d,"warmup":5,"inputs":1,"outputs":1,"output0":0.25,'
                    '"request_us":%s,"invoke_us":%s,"overhead_us":%s}'
                    % (runs, s % (404, 409, 400, 409, 405), s % (104, 109, 100, 109, 105), s % (300, 300, 300, 300, 300)),
                    "[MLBENCH] memory internal_free=34000 B internal_min_free=18000 B psram_free=1900000 B"]
        elif cmd == "TASKS":
            out = ["[TASK] SchedulerTask    id=1  core=1 pri=21 stack_free=11000 B",
                   "[TASK] heap_free=34012 B min_free=18052 B largest=20000 B console_rx_irqs=5 console_rx_dropped=0"]
            self.pending += [(line + "\n").encode() for line in out]
            out, self.after_tasks = self.after_tasks, []
        self.pending += [(line + "\n").encode() for line in out]

    def flush(self):
        pass

    def readline(self):
        return self.pending.pop(0) if self.pending else b""


def fast_lines(self, seconds):
    """Console.lines without waiting: stop when the fake board has nothing left."""
    while self.conn.pending:
        line = self.conn.readline().decode().strip()
        if line:
            self.log.append(d5_bench.redact(line))
            yield line


class TestD5Bench(unittest.TestCase):
    def setUp(self):
        self._lines = d5_bench.Console.lines
        d5_bench.Console.lines = fast_lines

    def tearDown(self):
        d5_bench.Console.lines = self._lines

    def test_stats_match_firmware(self):
        # The same cases as tests/test_ml_bench.c.
        self.assertEqual(d5_bench.stats([5, 1, 3]), {"median": 3, "p95": 5, "min": 1, "max": 5, "mean": 3})
        self.assertEqual(d5_bench.stats([4, 1, 2, 3]), {"median": 2, "p95": 4, "min": 1, "max": 4, "mean": 3})
        s = d5_bench.stats(range(1, 21))
        self.assertEqual((s["p95"], s["median"], s["mean"]), (19, 10, 11))
        self.assertIsNone(d5_bench.stats([]))

    def test_parsers(self):
        self.assertEqual(d5_bench.parse_arena("[TFLITE]   Arena:   2908 / 65536 bytes used"), (2908, 65536))
        self.assertEqual(d5_bench.parse_vector_calls("[ML-ACCEL] esp_nn_fc_s16_esp32s3 vector_calls=2"), 2)
        self.assertEqual(d5_bench.parse_run("[MLBENCH] run=7 request_us=812 invoke_us=150"), (7, 812, 150))
        self.assertIsNone(d5_bench.parse_run("[MLBENCH] runs=7 warmup=5"))
        self.assertEqual(d5_bench.parse_heap("[TASK] heap_free=40880 B min_free=31928 B largest=1 B x=1"),
                         {"heap_free": 40880, "min_free": 31928, "largest": 1})
        self.assertEqual(d5_bench.parse_ml_invoke('{"type":"ml","model":"sine-int8","invoke_us":812,"status":"OK"}'), 812)
        self.assertIsNone(d5_bench.parse_ml_invoke('{"type":"ml","outputs":null,"status":"MISSING_OR_STALE"}'))
        self.assertEqual(d5_bench.redact("joined ssid=MyPhone mac 0a:1b:2c:3d:4e:5f"), "joined ssid=<ssid> mac <mac>")

    def test_bench_session(self):
        board = FakeBoard()
        log = []
        results = d5_bench.bench_session(d5_bench.Console(board, log), "optimized", d5_bench.MODELS, 20)
        self.assertEqual(board.sent, ["STOP", "ML MODEL sine-float", "MLBENCH 20 RAW", "ML MODEL sine-int8",
                                      "MLBENCH 20 RAW", "ML MODEL autoencoder", "MLBENCH 20 RAW", "TASKS"])
        float_r, int8_r, _ = results["models"]
        self.assertEqual((float_r["vector_calls"], int8_r["vector_calls"]), (0, 2))
        self.assertEqual(int8_r["arena_bytes"], 1234)
        self.assertEqual(int8_r["invoke_us"]["median"], 104)
        self.assertEqual(len(int8_r["raw"]), 20)
        self.assertEqual(int8_r["memory"]["psram_free"], 1900000)
        self.assertTrue(all("error" not in r and "warning" not in r for r in results["models"]))
        self.assertEqual(results["tasks_heap"]["heap_free"], 34012)
        table = d5_bench.bench_table(results)
        self.assertIn("| sine-int8 | 1234 | 2 | 404 / 409 | 104 / 109 | 300 | 0.25 |", table)
        json.dumps(results)  # saved as results.json

    def test_wrong_build_and_failures_are_reported(self):
        results = d5_bench.bench_session(d5_bench.Console(FakeBoard(vector_calls=0), []), "optimized", ["sine-int8"], 20)
        self.assertIn("not called", results["models"][0]["warning"])
        results = d5_bench.bench_session(d5_bench.Console(FakeBoard(vector_calls=2), []), "ansi", ["sine-int8"], 20)
        self.assertIn("plain C build", results["models"][0]["warning"])
        results = d5_bench.bench_session(d5_bench.Console(FakeBoard(fail_model="autoencoder"), []), "optimized",
                                         ["autoencoder", "sine-int8"], 20)
        self.assertIn("ML: ERROR", results["models"][0]["error"])
        self.assertNotIn("error", results["models"][1])
        results = d5_bench.bench_session(d5_bench.Console(FakeBoard(runs_short=3), []), "optimized", ["sine-int8"], 20)
        self.assertIn("17 raw runs for 20", results["models"][0]["error"])

    def test_compare(self):
        opt = d5_bench.bench_session(d5_bench.Console(FakeBoard(), []), "optimized", ["sine-int8"], 20)
        ansi = json.loads(json.dumps(opt))
        ansi["models"][0]["invoke_us"]["median"] = 312
        table = d5_bench.compare_table(opt, ansi)
        self.assertIn("| sine-int8 | 104 | 312 | 3.00x | 404 | 404 | yes |", table)

    def test_live_session(self):
        board = FakeBoard()
        board.after_tasks = ['{"type":"ml","model":"sine-int8","invoke_us":%d,"status":"OK"}' % us
                             for us in (900, 800, 1000)]
        board.after_tasks.insert(1, '{"type":"ml","outputs":null,"status":"MISSING_OR_STALE"}')
        results = d5_bench.live_session(d5_bench.Console(board, []), 3)
        self.assertEqual(results["samples"], 3)
        self.assertEqual(results["invoke_us"], {"median": 900, "p95": 1000, "min": 800, "max": 1000, "mean": 900})


if __name__ == "__main__":
    unittest.main()
