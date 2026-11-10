"""Interactive pySerial monitor with compact ESP32 sensor and ML rows."""

import json
import re
import sys

from serial.tools import miniterm


SENSOR_FIELDS = (
    ("temperature_c", "Temp", " C", ".2f"),
    ("humidity_pct", "RH", "%", ".2f"),
    ("light_lux", "Light", " lux", ".0f"),
    ("distance_cm", "Distance", " cm", ".1f"),
)
RAW_SENSOR_FRAME = re.compile(r"^proto=(?:i2c|gpio) src=(.*?) len=\d+ ts=\d+ :")


def number(value, precision):
    return format(value, precision) if type(value) in (int, float) else "--"


def vector(values):
    if not isinstance(values, list):
        return "--"
    displayed = ", ".join(number(value, ".5g") for value in values[:4])
    suffix = f", +{len(values) - 4} more" if len(values) > 4 else ""
    return f"[{displayed}{suffix}]"


def telemetry_row(record):
    if not isinstance(record, dict):
        return None
    timestamp = record.get("timestamp_ms")
    status = record.get("status")
    if type(timestamp) is not int or timestamp < 0 or not isinstance(status, str):
        return None
    time = f"{timestamp // 1000:>6}.{timestamp % 1000:03d}s"

    if record.get("type") == "ml" and isinstance(record.get("model"), str):
        model = record["model"]
        if status == "MISSING_OR_STALE":
            return f"{time}  ML        Waiting for fresh inputs ({model})"
        if status != "OK":
            return f"{time}  ML        {status} ({model})"
        return (
            f"{time}  ML        OK  {model}"
            f"  in {vector(record.get('inputs'))}  out {vector(record.get('outputs'))}"
        )

    source = record.get("sensor")
    if not isinstance(source, str):
        return None
    readings = [
        f"{label} {number(record[key], precision)}{unit if record[key] is not None else ''}"
        for key, label, unit, precision in SENSOR_FIELDS
        if key in record
    ]
    if not readings:
        return None
    error = record.get("error")
    detail = f" (error {error})" if type(error) is int and error else ""
    return f"{time}  {source:<8}  {status:<8}  {'  |  '.join(readings)}{detail}"


class PrettyTelemetry(miniterm.Transform):
    """Format complete records, folding adjacent raw frames into decoded rows."""

    def __init__(self):
        self.partial = ""
        self.pending_frame = None

    def rx(self, text):
        lines = (self.partial + text).split("\n")
        self.partial = lines.pop()
        rendered = [self.format_line(line + "\n") for line in lines]
        if len(self.partial) > 4096:
            rendered.append(self.format_line(self.partial))
            self.partial = ""
        return "".join(rendered)

    def format_line(self, line):
        record = None
        row = None
        if line.startswith("{"):
            try:
                record = json.loads(line)
                row = telemetry_row(record)
            except (ValueError, OverflowError, RecursionError):
                pass

        prefix = ""
        if self.pending_frame:
            source, frame = self.pending_frame
            if row is None or record.get("sensor") != source:
                prefix = frame
            self.pending_frame = None

        if row is not None:
            return prefix + row + "\n"

        raw_frame = RAW_SENSOR_FRAME.match(line)
        if raw_frame:
            self.pending_frame = (raw_frame[1], line)
            return prefix
        return prefix + line


if __name__ == "__main__":
    miniterm.TRANSFORMATIONS["mtk-telemetry"] = PrettyTelemetry
    sys.argv.extend(["--filter", "mtk-telemetry"])
    miniterm.main()
