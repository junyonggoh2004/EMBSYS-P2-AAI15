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

TIMING_TIMEOUT = re.compile(r"^\[TIMING\] sensor=(.*?) status=timeout ")
SENSOR_ERROR = re.compile(r"\[SENSOR\] name=(.*?) error=(-?\d+) total=\d+")
HIDDEN_SENSOR_STATUSES = {"SENSOR_ERROR_OR_MISSING", "TIMEOUT"}


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
        outputs = record.get("outputs")
        row = f"{time}  ML        OK  {model}  in {vector(record.get('inputs'))}"
        exceeded = record.get("threshold_exceeded")
        if type(exceeded) is bool and isinstance(outputs, list) and len(outputs) == 1:
            cutoff = number(record.get("threshold"), ".9g")
            score = number(outputs[0], ".9g")
            decision = f"PASS (> {cutoff})" if exceeded else f"AT OR BELOW ({cutoff})"
            return f"{row}  out [{score}]  |  {decision}"
        return f"{row}  out {vector(outputs)}"

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
    if status in HIDDEN_SENSOR_STATUSES:
        return ""
    error = record.get("error")
    detail = f" (error {error})" if type(error) is int and error else ""
    return f"{time}  {source:<8}  {status:<8}  {'  |  '.join(readings)}{detail}"


class PrettyTelemetry(miniterm.Transform):
    """Format complete records, folding adjacent raw frames into decoded rows."""

    def __init__(self):
        self.partial = ""
        self.pending_frame = None
        self.pending_error = None
        self.last_reading_ms = None

    def rx(self, text):
        lines = (self.partial + text).split("\n")
        self.partial = lines.pop()
        rendered = [self.format_line(line + "\n") for line in lines]
        if len(self.partial) > 4096:
            rendered.append(self.format_line(self.partial))
            self.partial = ""
        return "".join(rendered)

    def format_line(self, line):
        previous_error = self.pending_error
        self.pending_error = None
        if previous_error is not None:
            diagnostic = SENSOR_ERROR.fullmatch(line.rstrip("\r\n"))
            if diagnostic and previous_error == (diagnostic[1], diagnostic[2]):
                return ""

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
            same_sensor = row is not None and record.get("sensor") == source
            if not same_sensor or (frame.startswith("[TIMING]") and row != ""):
                prefix = frame
            self.pending_frame = None

        if row == "":
            self.pending_error = (record["sensor"], str(record.get("error")))
            return prefix
        if row is not None:
            timestamp = record["timestamp_ms"]
            # The 2 s preset emits each sampling burst within a short window.
            previous = self.last_reading_ms
            self.last_reading_ms = timestamp
            separator = "\n" if previous is not None and (timestamp < previous or timestamp - previous >= 1000) else ""
            return prefix + separator + row + "\n"

        raw_frame = RAW_SENSOR_FRAME.match(line) or TIMING_TIMEOUT.match(line)
        if raw_frame:
            self.pending_frame = (raw_frame[1], line)
            return prefix
        return prefix + line


if __name__ == "__main__":
    miniterm.TRANSFORMATIONS["mtk-telemetry"] = PrettyTelemetry
    sys.argv.extend(["--filter", "mtk-telemetry"])
    miniterm.main()
