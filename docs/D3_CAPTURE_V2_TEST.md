# D3-2 asynchronous sonar validation: V2 fixture

Baseline: [breadboard guide](../SENSOR_BREADBOARD_README.md), [active wiring](../esp32s3_mtk/WIRING.md),
[D3 task list](../README.md#4-d3-task-checklist) and
[interrupt review](../D3_INTERRUPT_WIRING_REVIEW.md). This procedure preserves
the existing capture implementation; it changes host test configuration only.

## Required fixture

Use the documented 3.3 V-compatible HC-SR04+: VCC 3V3, common GND,
Trig GPIO4, Echo GPIO5. Verify actual module and Echo voltage before connecting
or running. Standard 5 V Echo must retain voltage protection. GPIO1/2 belong to
I2C and GPIO6–10 retain LCD wiring. No resistor removal is part of these tests.
Close serial monitors first. Tests replace volatile sensor/rule configuration.

## Build and connected single-sonar tests

From the repository root on macOS/Linux, run the pinned build and image gates:

```sh
sh tools/mtk.sh check
sh tools/mtk.sh image
```

Flash the resulting image after fixture verification and retain image hash,
source commit, board ID and boot log. Open the monitor to confirm HELP/SHOW/TASKS
and successful startup, then close it before running the following commands.
Replace `<port>` with the board's actual serial port; do not type the brackets.

```sh
.venv/bin/python parity/parity_test.py --board s3 --fixture v2-sonar --stage 4c-sonic --port <port> --log parity/logs/mtk/d3/v2-sonic.log
.venv/bin/python parity/parity_test.py --board s3 --fixture v2-sonar --stage sonic-rule --port <port> --log parity/logs/mtk/d3/v2-sonic-rule.log
```

On Windows use `.venv/Scripts/python.exe` and the documented pinned PowerShell
build/flash helper. Both selected stages explicitly STOP and CLEAR first, map
only the sonar to GPIO4/5 and retain the existing distance/rule assertions.
Expected totals are 6/6 for `4c-sonic` and 9/9 for `sonic-rule` (two fixture setup
checks added). Move an object in range for capture, and across 30 cm for rules.
The default `sh tools/mtk.sh test` continues to use upstream fixture mappings;
use the explicit Python commands above for V2.

## Single capture, missing Echo and cancellation

Paste the non-comment lines of
[config/d3_sonar_v2_once.cfg](../config/d3_sonar_v2_once.cfg) into the monitor
one at a time and check CFG acknowledgement. It ends with SHOW, not RUN.
Send RUN to take one observation, then `TRIGGER D3_SONAR` to take another.
Expect exactly one valid width frame per successful arm. This is a manual
one-shot probe; it does not replace the real-window acceptance tests.

For a missing-Echo case, power off before disconnecting the module's Echo lead,
restore power, reload the same probe and send RUN. Internal pulldown supplies
idle bias. Expect explicit `error=-1` and no valid distance frame; SHOW/TASKS
must still respond. This is the sonar portion of the `t06` requirement on V2,
not a claim that the full historical `t06` ran. The original stage additionally
touches GPIO6/7 and is refused with `--fixture v2-sonar`.

For STOP/CLEAR cancellation, retain a scope/logic-analyzer trace of Trig, Echo
and serial commands. Send STOP or CLEAR during a verified pending acquisition,
then confirm Trig returns low and no completion frame from that arm is
published. Reload/rearm and repeat. A command sent after capture ended does not
prove pending cancellation, and serial acknowledgement alone does not establish
the exact capture state. Native lifecycle tests provide complementary evidence.
Do not inject signals until their levels and connection are verified.

Record false/extra/missing edges, scope width versus reported width, timeout
counts, command timing and scheduler progress. Use a stimulus appropriate to
the module's specified range; do not call missing Echo a valid clear reading.
After tests, power off before reconnecting Echo and restore the documented
three-sensor preset as needed.

## Remaining acceptance

Capture TASKS core/stack/heap and repeated configure/CLEAR resource behavior.
Run D1 regression on its separate verified fixtures; do not run their arbitrary
GPIO commands against the retained LCD/I2C harness. A second sonar, explicit
guard and reduced pair timeouts are needed before PAIR testing. The preset's
two 30 ms capture budgets would exceed the complete 50 ms pair ceiling.
This procedure does not select a guard or establish crosstalk/WCET margins.

Host checks for this fixture selection:

```sh
.venv/bin/python -m unittest discover -s tests -p test_sonar_fixture.py
sh tools/cody_test.sh
sh tools/d2_host_test.sh
sh tools/mtk.sh purity
```

Only completed target runs may close README D3-2. Native tests, generated
configuration and a build alone are insufficient.

## Executed preflight — 5 November 2026

Firmware source `11255ec`: pinned course port/ESP-IDF/compiler matched and the
build passed after retrying outside the sandbox's process-inspection restriction.
Application image: `0x13b590` bytes, 58% partition free. Post-build image purity
and the configurable-image audit passed: µT-Kernel startup/dispatcher and sensor
profiles linked, original FreeRTOS scheduler excluded, PSRAM/native USB enabled.
This does not establish runtime heap or stack headroom.

Existing D3 native suites, D2 host tests and five new fixture/probe tests pass.
The fixture tests verify unchanged upstream mappings/assertions, GPIO4/5-only
selection, STOP/CLEAR before configuration, rejection of unrelated stages/boards
and a one-shot preset that does not auto-run. Raw output and image hashes:
[preflight evidence](../parity/logs/d3/d3-2-preflight-20261105.log).

No ESP32-S3 was detected on USB. No flash, actual capture, missing-Echo,
STOP/CLEAR-in-flight, sonar rule or TASKS board run was performed. D3-2 remains
**code/native validated; physical acceptance pending**. The original standalone
`t06` and dual-sonar PAIR were not run on the retained V2 harness.
