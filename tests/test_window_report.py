import io
import os
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "parity"))
from application_test import CheckFailure, Console
from window_report import parse_report, collect
from test_application_smoke import Clock, SerialFixture

TOKEN = "abcdef01"


def report(token=TOKEN, n=7, valid=7, cancelled=0, misses=0,
           minimum=13000, mean=13000, maximum=13000, p95="13000", p99="13000", events=1):
    rows = [
        f"state version=1 configured=1 enabled=0 state=2 events={events} dropped=0 radar_frames=7 malformed=0 rx_dropped=0 rx_dropped_start=0 identity=1 event_schema=2 now_us=1613000",
        f"timing pairs={n} valid={valid} failed=0 cancelled={cancelled} deadline_misses={misses} skipped=0 min_us={minimum} mean_us={mean} max_us={maximum} p95_le_us={p95} p99_le_us={p99}",
        f"events entry={events} exit=0 incomplete=0 false_events=0 window_deadlines=0 radar_stale=0 window_faults=0",
        "config period_us=100000 window_us=2000000 pair_limit_us=50000 clear_us=200000 radar_stale_us=300000 max_pairs=20 guard_us=1000 timeout_a_us=12000 timeout_b_us=12000 occupied_a_mm=500 occupied_b_mm=500",
        "geometry a_name=A a_trig=8 a_echo=9 b_name=B b_trig=10 b_echo=11 radar_name=RADAR radar_tx=12 radar_rx=13 xmin=-500 xmax=500 ymin=0 ymax=2000",
        "end",
    ]
    return [f"[WINDOW_REPORT] token={token} kind={row}" for row in rows]


class WindowReportTests(unittest.TestCase):
    def test_fresh_nonce_partial_reads_and_only_read_command(self):
        stale = "\n".join(report("11111111")) + "\n"
        current = "\n".join(report()) + "\n"
        port = SerialFixture([stale.encode(), current[:120].encode(), current[120:].encode()])
        result = collect(Console(port, io.StringIO(), Clock()), TOKEN)
        self.assertEqual(port.sent, [b"WINDOW STATUS abcdef01\n"])
        self.assertEqual(result["timing"]["pairs"], 7)
        self.assertEqual(result["timing"]["p99_le_us"], 13000)
        self.assertFalse(result["pair_budget_exceeded"])
        self.assertEqual(result["acceptance"], "not_evaluated")

    def test_no_samples_is_not_a_budget_pass(self):
        result = parse_report(report(n=0, valid=0, minimum=0, mean=0, maximum=0,
                                     p95="0", p99="0", events=0), TOKEN)
        self.assertIsNone(result["pair_budget_exceeded"])
        self.assertIsNone(result["timing"]["p95_le_us"])

    def test_overdue_cancel_remains_in_timing(self):
        result = parse_report(report(n=8, cancelled=1, misses=1, mean=19500,
                                     maximum=65001, p95=">50000", p99=">50000"), TOKEN)
        self.assertTrue(result["pair_budget_exceeded"])
        self.assertTrue(result["timing"]["p99_overflow"])
        self.assertIsNone(result["timing"]["p99_le_us"])
        self.assertEqual(result["timing"]["cancelled"], 1)

    def test_missing_mixed_or_duplicate_sections_fail(self):
        cases = [report()[:-1], report()[:-2] + report("11111111")[-2:], report() + report()[:1]]
        for rows in cases:
            with self.subTest(rows=rows), self.assertRaises(CheckFailure):
                parse_report(rows, TOKEN)

    def test_inconsistent_counters_and_bounds_fail(self):
        for rows in [report(valid=6), report(p95="12345"), report(p95=">50000"),
                     report(p95="14000", p99="13000"), report(n=0, valid=0)]:
            with self.subTest(rows=rows), self.assertRaises(CheckFailure):
                parse_report(rows, TOKEN)

    def test_running_refusal_is_explicit(self):
        rows = f"[WINDOW_REPORT] token={TOKEN} kind=error code=-6\n[WINDOW_REPORT] token={TOKEN} kind=end\n"
        console = Console(SerialFixture([rows.encode()]), io.StringIO(), Clock())
        with self.assertRaisesRegex(CheckFailure, "STOP sampling"):
            collect(console, TOKEN)

    def test_reset_or_timeout_cannot_be_success(self):
        reset = ("\n".join(report()) + "\n[BOOT] restarting\n").encode()
        with self.assertRaisesRegex(CheckFailure, "unexpected reset"):
            collect(Console(SerialFixture([reset]), io.StringIO(), Clock()), TOKEN)
        with self.assertRaisesRegex(CheckFailure, "missing replies"):
            collect(Console(SerialFixture([]), io.StringIO(), Clock()), TOKEN)

    @unittest.skipUnless(os.environ.get("D3_NATIVE_REPORT_LOG"), "run tools/cody_test.sh for native producer integration")
    def test_actual_native_firmware_report(self):
        lines = Path(os.environ["D3_NATIVE_REPORT_LOG"]).read_text().splitlines()
        result = parse_report(lines, TOKEN)
        self.assertEqual(result["timing"]["pairs"], 7)
        self.assertEqual(result["events"]["entry"], 1)
        self.assertEqual(result["timing"]["max_us"], 13000)
        self.assertEqual(result["geometry"]["radar_name"], "RADAR")


if __name__ == "__main__":
    unittest.main()
