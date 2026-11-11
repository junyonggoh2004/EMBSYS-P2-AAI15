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

    def test_missing_sensor_rows_and_duplicates_are_hidden(self):
        monitor = PrettyTelemetry()
        output = monitor.rx(
            '[TIMING] sensor=DISTANCE status=timeout phase=acquisition deadline_us=68351000\n'
            '{"sensor":"DISTANCE","timestamp_ms":68351,"distance_cm":null,'
            '"status":"TIMEOUT","error":-1}\n'
            '[SENSOR] name=DISTANCE error=-1 total=1\n'
            '{"sensor":"AHT","timestamp_ms":68352,"temperature_c":null,"humidity_pct":null,'
            '"status":"SENSOR_ERROR_OR_MISSING","error":-4}\n'
            '[SENSOR] name=AHT error=-4 total=2\n'
            '{"sensor":"LIGHT","timestamp_ms":68352,"light_lux":0,"status":"OK","error":0}\n'
            '{"type":"ml","timestamp_ms":68352,"model":"sine-float",'
            '"status":"MISSING_OR_STALE","outputs":null}\n'
            '{"type":"ml","timestamp_ms":70352,"model":"sine-float",'
            '"status":"OK","inputs":[1.829175],"outputs":[0.95309836]}\n'
        )
        self.assertNotIn("DISTANCE", output)
        self.assertNotIn("AHT", output)
        self.assertNotIn("SENSOR_ERROR_OR_MISSING", output)
        self.assertIn("LIGHT     OK        Light 0 lux", output)
        self.assertIn("Waiting for fresh inputs (sine-float)", output)
        self.assertIn("in [1.8292]  out [0.9531]", output)
        first_burst, next_burst = output.split("\n\n")
        self.assertEqual(len(first_burst.splitlines()), 2)
        self.assertEqual(len(next_burst.splitlines()), 1)

    def test_scalar_output_threshold_decisions(self):
        monitor = PrettyTelemetry()
        output = monitor.rx(
            '{"type":"ml","timestamp_ms":1000,"model":"sine-int8","status":"OK",'
            '"outputs":[0.5554941],"threshold":0.5,"threshold_exceeded":true}\n'
            '{"type":"ml","timestamp_ms":3000,"model":"sine-float","status":"OK",'
            '"outputs":[0.5],"threshold":0.5,"threshold_exceeded":false}\n'
        )
        self.assertIn("out [0.5554941]  |  PASS (> 0.5)", output)
        self.assertIn("out [0.5]  |  AT OR BELOW (0.5)", output)

    def test_diagnostics_spi_and_unknown_payloads_pass_through(self):
        monitor = PrettyTelemetry()
        original = (
            "proto=i2c src=GENERIC len=1 ts=1 : AB\r\n"
            "proto=spi src=FLASH len=1 ts=2 : A5\r\n"
            '[TIMING] sensor=FLASH status=timeout phase=acquisition deadline_us=12\n'
            '[SENSOR] name=FLASH error=-1 total=1\n'
            'ML: OK RUN\r\n{"type":"other"}\n{incomplete-json}\n'
        )
        self.assertEqual(monitor.rx(original), original)
        self.assertEqual(monitor.tx("RUN\n"), "RUN\n")


if __name__ == "__main__":
    unittest.main()
