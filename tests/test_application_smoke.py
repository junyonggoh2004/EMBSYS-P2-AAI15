import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "parity"))
from application_test import CheckFailure, Console, application_reply, validate_tasks


class SerialFixture:
    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.sent = []

    def read(self, size):
        return self.chunks.pop(0) if self.chunks else b""

    def write(self, data):
        self.sent.append(data)


class Clock:
    def __init__(self):
        self.now = 0

    def __call__(self):
        self.now += 0.01
        return self.now


class ApplicationSmokeTests(unittest.TestCase):
    def test_split_lines_noise_and_wrong_old_result(self):
        serial = SerialFixture([
            b"[MQTT] Status: Connected\r\n[APP] id=101 seq=1 result=0 class=entry "
            b"occupancy=0->1 degraded=0 reason=accepted publish=1\n",
            b"[APP] id=101 seq=1 result=2 class=unknown occupancy=1->",
            b"1 degraded=0 reason=duplicate publish=1\r\n",
        ])
        log = io.StringIO()
        console = Console(serial, log, Clock())
        lines = console.exchange("APP EVENT 101 1 entry", [application_reply(101, 2, 1)])
        self.assertEqual(serial.sent, [b"APP EVENT 101 1 entry\n"])
        self.assertEqual(len(lines), 3)
        self.assertIn("reason=duplicate", log.getvalue())

    def test_every_pattern_is_required(self):
        console = Console(SerialFixture([b"RUN\n"]), io.StringIO(), Clock())
        with self.assertRaisesRegex(CheckFailure, "missing replies"):
            console.exchange("RUN", [r"^RUN$", application_reply(r"\d+", 4, 0, "quality")], 0.1)

    def test_runtime_reset_cannot_look_like_success(self):
        console = Console(SerialFixture([b"[BOOT] restarting\nRUN\n"]), io.StringIO(), Clock())
        console.monitor_runtime = True
        with self.assertRaisesRegex(CheckFailure, "unexpected reset"):
            console.exchange("RUN", [r"^RUN$"])

    @staticmethod
    def tasks(core=1, priority=23, core0=0, core1=17, stack=2300,
              message=88, buffer=736):
        return [
            f"[TASK] app_message={message} B app_buffer={buffer} B",
            f"[TASK] ApplicationTask  id=8  core={core} pri={priority} stack_free={stack} B "
            f"ran_on_core0={core0} ran_on_core1={core1}",
        ]

    def test_task_placement_and_kernel_storage(self):
        result = validate_tasks(self.tasks())
        self.assertEqual(result["message_bytes"], 88)
        self.assertEqual(result["buffer_bytes"], 736)
        for changes in [{"core": 0}, {"priority": 22}, {"core0": 1}, {"core1": 0},
                        {"stack": 0}, {"message": 64, "buffer": 544}, {"buffer": 704}]:
            with self.subTest(changes=changes), self.assertRaises(CheckFailure):
                validate_tasks(self.tasks(**changes))
        with self.assertRaises(CheckFailure):
            validate_tasks([])


if __name__ == "__main__":
    unittest.main()
