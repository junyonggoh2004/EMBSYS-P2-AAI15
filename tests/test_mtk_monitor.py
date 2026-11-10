from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "esp32s3_mtk/tools"))
from monitor import PrettyTelemetry


class MonitorTests(unittest.TestCase):
    def test_fragmented_decoded_frame_replaces_raw_duplicate(self):
        monitor = PrettyTelemetry()
        self.assertEqual(monitor.rx("proto=i2c src=ROOM len=6 ts=66401 : 18 93\r\n{"), "")
        output = monitor.rx(
            '"sensor":"ROOM","timestamp_ms":66406,"temperature_c":26.26,'
            '"humidity_pct":57.63,"status":"OK","error":0}\r\n'
        )
        self.assertEqual(output, "    66.406s  ROOM      OK        Temp 26.26 C  |  RH 57.63%\n")

    def test_ml_and_timeout_remain_distinct(self):
        monitor = PrettyTelemetry()
        output = monitor.rx(
            '{"sensor":"DISTANCE","timestamp_ms":68351,"distance_cm":null,'
            '"status":"TIMEOUT","error":-1}\n'
            '{"type":"ml","timestamp_ms":68352,"model":"sine-float",'
            '"status":"MISSING_OR_STALE","outputs":null}\n'
            '{"type":"ml","timestamp_ms":70352,"model":"sine-float",'
            '"status":"OK","inputs":[1.829175],"outputs":[0.95309836]}\n'
        )
        self.assertIn("DISTANCE  TIMEOUT   Distance -- (error -1)", output)
        self.assertIn("Waiting for fresh inputs (sine-float)", output)
        self.assertIn("in [1.8292]  out [0.9531]", output)

    def test_diagnostics_spi_and_unknown_payloads_pass_through(self):
        monitor = PrettyTelemetry()
        original = (
            "proto=i2c src=GENERIC len=1 ts=1 : AB\r\n"
            "proto=spi src=FLASH len=1 ts=2 : A5\r\n"
            'ML: OK RUN\r\n{"type":"other"}\n{incomplete-json}\n'
        )
        self.assertEqual(monitor.rx(original), original)
        self.assertEqual(monitor.tx("RUN\n"), "RUN\n")


if __name__ == "__main__":
    unittest.main()
