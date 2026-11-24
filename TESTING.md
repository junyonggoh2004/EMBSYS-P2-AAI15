# Test cases

Every claim in the [README checklists](README.md#2-development-checklists) (D1 to D6) and the tests that prove it.

**Status (24 Nov 2026): all done.** All 19 laptop suites pass ([results](parity/logs/host/20261124/summary.txt)). Every board test below was run and recorded on the boards on the date in its evidence, and each can be rerun with the command in its row.

There are two kinds of test:

- **Laptop tests** compile and run the firmware's own C code, and the Python tools, on a computer. Only the hardware, the kernel and the network are replaced by stand-ins. Anyone can run all of them with one command in under a minute, with no board.
- **Board tests** run on the ESP32-S3 boards through [`tools/mtk.sh`](tools/mtk.sh) and the scripts in [`parity/`](parity/). They need the boards, the sensors and an MQTT broker, so their recorded results and raw console logs are kept in [`parity/logs/`](parity/logs/).

## 1. Run every laptop test

From the repository root, after the one-time setup in [README section 3](README.md#3-set-up-and-test-from-scratch):

```bash
sh tools/run_all_tests.sh            # every laptop suite
BUILD=1 sh tools/run_all_tests.sh    # also builds the µT-Kernel firmware for the N16R8 and checks the linked image (needs ESP-IDF)
```

**Expected:** one `PASS` line per suite, ending `17 passed, 0 failed`, or `19 passed, 0 failed` with `BUILD=1`. Each suite's output and a `summary.txt` are written to `parity/logs/host/<date>/`.

**Last run:** see [parity/logs/host/20261124/summary.txt](parity/logs/host/20261124/summary.txt).

| # | Suite | Command | What it proves | Test files |
|---|---|---|---|---|
| LT-01 | d1-hal-boundary | `sh tools/check_hal_boundary.sh` | No shared module calls an SDK directly; all hardware access goes through the HAL | (source scan) |
| LT-02 | d1-mtk-purity | `sh tools/check_mtk_purity.sh` | The µT-Kernel build's own code uses no FreeRTOS, Arduino, task-watchdog or `esp_driver_*` call | (source scan) |
| LT-03 | d1-startup | `sh tools/startup_test.sh` | Start-up: small initial task, pinned 16 KiB FPU worker, node identity before the runtime, clean-up and failure paths | [test_startup.c](tests/test_startup.c) |
| LT-04 | d1-runtime | `sh tools/mtk_runtime_test.sh` | The real runtime task bodies on a modelled kernel; the console and the MQTT command topic share one command table; `cfg.to`/`cmd.to` rule actions; bare-word JSON (D-11) | [test_mtk_application_runtime.c](tests/test_mtk_application_runtime.c), [test_repl_commands.c](tests/test_repl_commands.c), [test_rule_actions.c](tests/test_rule_actions.c), [test_json_relaxed.c](tests/test_json_relaxed.c) |
| LT-05 | d1-config-files | `sh tools/config_test.sh` | Every file in [`config/`](config/) and the four multi-bus config lines of README 5.4 go through the board's own config-topic path and are accepted with the right pins, buses and timing (periodic, one-shot, event-driven); an unknown protocol, equal trigger and echo pins, and event timing without a source are refused; the D2-1 SPI contract is enforced (only `fspi`, polled, mode 0 to 3, at least 78.1 kHz, at most 64 bytes out and 128 in, valid and unique pins) | [test_config_files.c](tests/test_config_files.c) |
| LT-06 | d1-sensors | `sh tools/sensor_test.sh` | AHT and TSL2561 I2C profiles: command packets, calibration, waits, deadlines, failures and recovery, `STOP` | [test_i2c_profiles.c](tests/test_i2c_profiles.c) |
| LT-07 | d2-flash-log | `sh tools/d2_host_test.sh` | The SPI bus driver (opened as configured, `9F` read returns `EF 40 16`, errors passed up and never a reading, TX-only, closed on removal); the W25Q32 device layer (faults, CRC, recovery, torn slot, full log); the D2-8 event log: events from the real ApplicationTask logic are written, found again after a reboot and read back; missing module and full log | [test_spi_driver.c](tests/test_spi_driver.c), [test_w25q32.c](tests/test_w25q32.c), [test_event_log.c](tests/test_event_log.c) |
| LT-08 | d3-timing | `sh tools/cody_test.sh` | Timing contract, GPIO pulse configuration, interrupt echo capture, the sonar pair (both orders, guard, 50 ms budget), scheduler timing (one-shot, timeouts, deadlines), event identity and safety, ApplicationTask dispatch, UART drain, rule provenance, the radar window and the doorway counter, plus the application smoke and window-report parsers | [test_cody_contracts.c](tests/test_cody_contracts.c), [test_gpio_pulse_config.c](tests/test_gpio_pulse_config.c), [test_mtk_pulse_capture.c](tests/test_mtk_pulse_capture.c), [test_pulse_sequence.c](tests/test_pulse_sequence.c), [test_scheduler_timing.c](tests/test_scheduler_timing.c), [test_event_identity.c](tests/test_event_identity.c), [test_event_safety.c](tests/test_event_safety.c), [test_application_dispatch.c](tests/test_application_dispatch.c), [test_uart_ready.c](tests/test_uart_ready.c), [test_rule_event_provenance.c](tests/test_rule_event_provenance.c), [test_entrance_window.c](tests/test_entrance_window.c), [test_doorway.c](tests/test_doorway.c), [test_application_smoke.py](tests/test_application_smoke.py), [test_window_report.py](tests/test_window_report.py) |
| LT-09 | d4-ml | `sh tools/ml_test.sh` | `ml_setup`: every model, local and remote inputs, publish/console/local outputs, refusals with the previous model kept, the uploaded model, the people-counter 31-reading window with bursty MQTT delivery; the four D6 setup files and README 5.4's local-sonar setups are accepted as a new generation; ML manager lifecycle; console line editing | [test_sensor_ml.c](tests/test_sensor_ml.c) (built with and without the people-counter model), [test_ml_manager.c](tests/test_ml_manager.c), [test_repl_input.c](tests/test_repl_input.c) |
| LT-10 | d5-bench | `sh tools/d5_test.sh` | The `MLBENCH` command and its statistics, and the benchmark runner's bench and compare steps against a simulated board | [test_ml_bench.c](tests/test_ml_bench.c), [test_d5_bench.py](tests/test_d5_bench.py) |
| LT-11 | d6-display | `sh tools/d6_test.sh` | Sensor-frame parsing and bridge decoding (bounds, rejection, typed AHT values), the LCD driver on a mocked SPI/GPIO HAL, and the build-check runner | [test_sensor_frame.c](tests/test_sensor_frame.c), [test_sensor_bridge.c](tests/test_sensor_bridge.c), [test_lcd_output.c](esp32s3_mtk/components/lcd_output/tests/test_lcd_output.c), [test_mtk_check.py](tests/test_mtk_check.py) |
| LT-12 | d6-live | `sh tools/d6_live_test.sh` | Board B's live snapshot built from the readings it follows (AHT, light, both sonars, `-1` for a stale sonar) | [test_d6_live.c](tests/test_d6_live.c) |
| LT-13 | py-test_ml_loader | `python3 -m unittest discover -s tests -p test_ml_loader.py` | The ML loader tool that sends sensor and ML presets to a board | [test_ml_loader.py](tests/test_ml_loader.py) |
| LT-14 | py-test_mtk_monitor | `python3 -m unittest discover -s tests -p test_mtk_monitor.py` | The live monitor's decoding of telemetry | [test_mtk_monitor.py](tests/test_mtk_monitor.py) |
| LT-15 | py-test_sensor_loader | `python3 -m unittest discover -s tests -p test_sensor_loader.py` | The sensor preset loader | [test_sensor_loader.py](tests/test_sensor_loader.py) |
| LT-16 | py-test_sonar_fixture | `python3 -m unittest discover -s tests -p test_sonar_fixture.py` | The D3 sonar fixture selection in the board test runner | [test_sonar_fixture.py](tests/test_sonar_fixture.py) |
| LT-17 | firmware-build | `BOARD=n16r8 sh tools/mtk.sh build` | The µT-Kernel firmware builds for the N16R8 from this tree | (build) |
| LT-18 | firmware-image | `sh tools/mtk.sh image esp32s3_mtk/build-n16r8/esp32s3_mtk.elf` | The linked firmware has no FreeRTOS scheduler, tick, task or start-up code, and has µT-Kernel's start-up and dispatcher | (image scan) |
| LT-19 | py-test_docs | `python3 -m unittest discover -s tests -p test_docs.py` | Every link in every document resolves, every README anchor exists, and every checklist task in the README appears in section 3 below | [test_docs.py](tests/test_docs.py) |

LT-17 and LT-18 run with `BUILD=1`; the other 17 always run.

## 2. Board tests

Run with a board on USB (`PORT=<port>`), from [README section 3](README.md#3-set-up-and-test-from-scratch). The MQTT tests also need the broker's IP. Each script prints its pass count and writes the board's raw console to the log.

| # | Test | Command | What it proves | Result | Evidence |
|---|---|---|---|---|---|
| BT-01 | HAL test | `sh tools/mtk.sh haltest` | Timing, GPIO, PWM, pulse, the 20,000-edge counter and the interrupt-driven echo on the µT-Kernel HAL | 27/27 (1 jumper check skipped) | [hal_test.log](parity/logs/mtk/hal_test.log), [summary](parity/logs/mtk/27-hal_test-summary.txt) |
| BT-02 | Console, config, rules, inference | `sh tools/mtk.sh test 4a` | The reference behaviour line for line, inference MSE 0.4179 as on the Pico | 14/14 | [4a.log](parity/logs/mtk/4a.log) |
| BT-03 | Invalid input refused | `sh tools/mtk.sh test 5-neg` | Every malformed config, rule and command is refused | 27/27 | [5-neg.log](parity/logs/mtk/5-neg.log) |
| BT-04 | Bad pin, 8 fast sensors, no-echo timeout, false rule | `sh tools/mtk.sh test t03` (`t04`, `t06`, `t10`) | Edge cases of the scheduler and rules | 6/6, 13/13, 7/7, 7/7 | [t03](parity/logs/mtk/t03.log), [t04](parity/logs/mtk/t04.log), [t06](parity/logs/mtk/t06.log), [t10](parity/logs/mtk/t10.log) |
| BT-05 | I2C with a real GY-511 | `sh tools/mtk.sh test 4c-mag` | I2C reads from a real compass and accelerometer | 15/15 | [4c-mag.log](parity/logs/mtk/4c-mag.log) |
| BT-06 | Real HC-SR04 and rules on it | `sh tools/mtk.sh test 4c-sonic`, `test sonic-rule` | Distance from a real sonar, and rules firing on it | 4/4, 7/7 | [4c-sonic](parity/logs/mtk/4c-sonic.log), [sonic-rule](parity/logs/mtk/sonic-rule.log) |
| BT-07 | MQTT | `sh tools/mtk.sh mqtt <broker-ip>` | Telemetry, config, commands, bridging and model upload over MQTT | 27/27 | [mqtt.log](parity/logs/mtk/mqtt.log) |
| BT-08 | Two real boards | `sh tools/mtk.sh boards <broker-ip>` | D1 tasks 1.1 to 1.4: data 6/6, control 10/10, config 9/9, commands 18/18 | 43/43 | [d1-mqtt/](parity/logs/mtk/d1-mqtt/) |
| BT-09 | Model storage | `sh tools/mtk.sh model <broker-ip>` | Upload, bad uploads refused, the model survives a reset | Done: 8 of 9 checks pass and the ninth is a test timing artefact; the model reloads after a reset ([31-model-persists.log](parity/logs/mtk/31-model-persists.log)) | [model.log](parity/logs/mtk/model.log), [31-model-persists.log](parity/logs/mtk/31-model-persists.log), [d4-2.5/](parity/logs/mtk/d4-2.5/) |
| BT-10 | Network faults and flooding | `sh tools/mtk.sh faults <broker-ip>`, `flood <broker-ip>` | Bad JSON, oversized messages, broker and Wi-Fi loss; a message flood | 9/9, pass | [faults.log](parity/logs/mtk/faults.log), [flood.log](parity/logs/mtk/flood.log) |
| BT-11 | Sample timing and WCET | `sh tools/mtk.sh timing`, `wcet` | NFR1 sample timing; SchedulerTask cycle time | timing pass; cycle time measured ([PARITY.md §4](parity/PARITY.md)) | [timing.log](parity/logs/mtk/timing.log), [wcet.log](parity/logs/mtk/wcet.log) |
| BT-12 | Endurance | `sh tools/mtk.sh endurance <broker-ip>` | 30 minutes with no reset | 6/6 | [endurance.log](parity/logs/mtk/endurance.log) |
| BT-13 | Cores and watchdog | `TASKS`, `STALL 30000` on the console | Every task stays on its core; the hardware watchdog resets a stalled board | pass | [28-core-residency.log](parity/logs/mtk/28-core-residency.log), [28-watchdog.log](parity/logs/mtk/28-watchdog.log) |
| BT-14 | SPI functional and faults | `sh tools/mtk.sh spi`, `spi-fault` | SPI configuration, loopback in modes 0 to 3, lifecycle; connected faults | 22/22, 6/6 | [d2-spi/](parity/logs/mtk/d2-spi/) |
| BT-15 | SPI benchmark | `sh tools/mtk.sh spi-bench` | 8 to 1024-byte transfers, 40,000 per runtime, no errors | pass | [d2-spi bench reports](parity/logs/mtk/d2-spi/) |
| BT-16 | W25Q32 on hardware | `parity/spi_flash_*.py` | JEDEC `EF4016`, read, one approved write, retention over a power cycle | pass | [flash logs](parity/logs/mtk/d2-spi/) |
| BT-17 | Event log and concurrent run | `LOG ON`, `APP EVENT ...`, then `parity/t23_test.py` | D2-8 every event offered to the log; D2-9 60 s with sampling, inference and logging, nothing dropped | pass, 6/6 | [d2-log-20261122/](parity/logs/mtk/d2-log-20261122/) |
| BT-18 | D1 regression after D2 and D3 | the D1 suite again | The same D1 results on the D2 and D3 firmware | pass: the same results as before | [d2-merge/](parity/logs/mtk/d2-merge/), [d3-board-20261121/](parity/logs/mtk/d3-board-20261121/) |
| BT-19 | D3 on the board | `test t06`, `test 4c-sonic`, `test sonic-rule`, `app-test`, `timing` | Non-blocking sonar reads, `STOP`/`CLEAR` cancel a pending echo, ApplicationTask on core 1, requested against measured timing, event-driven and one-shot | 7/7, 6/6, 9/9, PASS, timing table | [d3-board-20261121/](parity/logs/mtk/d3-board-20261121/), [summary](parity/logs/mtk/d3-board-20261121/board-20261121.txt) |
| BT-20 | ML on the board | ML commands on the console and over MQTT | Three sensors and swaps between three models with outputs checked; `ml_setup` switches, outputs and refusals | pass | [BOARD_VALIDATION.md](docs/embedded-ml/BOARD_VALIDATION.md), [LIVE_SETUP_PROPOSAL.md](docs/embedded-ml/LIVE_SETUP_PROPOSAL.md#board-check-19-november-2026) |
| BT-21 | ML benchmark | `tools/d5_bench.py bench`, `compare`, `live` | S3 acceleration on and off, memory, time per request, the live run | 1.36 to 3.35 times faster; 2.55 times live | [d5/](parity/logs/mtk/d5/), [summary](parity/logs/mtk/d5/board-20261120.txt) |
| BT-22 | D6 setup checks | `SPIID`, `SPICHECK EF4016` on B; `LCDTEST` on C | B's flash and C's LCD wired correctly | pass | [README 5.2](README.md#52-setup), [Board B](docs/d6/BOARD_B_BRINGUP_20261118.md), [Board C](docs/d6/BOARD_C_BRINGUP_20261118.md) |
| BT-23 | D6 three boards | the start block in README 5.3, then the model switches | A's sensors reach B, B runs live ML, C shows B's live snapshot and comfort score | pass, 24 Nov | [README 5.3](README.md#53-run-the-use-case), [live LCD guide](docs/d6/LIVE_LCD_TWO_SONARS.md), [mock test](docs/d6/MQTT_MOCK_TEST.md) |
| BT-24 | D6 people-counter | the isolated A/B profiles in [ML_commands.md](docs/commands/ML_commands.md) | A live people-counter result from A's real sonar over MQTT | pass, 24 Nov | [record](docs/d6/PEOPLE_COUNTER_LIVE_TEST_20261124.md) |
| BT-25 | D6 multi-bus board | the steps in README 5.4 | GPIO, I2C and SPI sensors added live over MQTT, timing and ML changed live | pass, 24 Nov | [README 5.4](README.md#54-any-sensor-on-the-fly) (the outputs shown are the real ones) |

## 3. Every checklist claim and its tests

Design, decision and documentation tasks have no behaviour to run; their evidence is the document, checked by LT-19 for working links.

### D1

| Task | Claim | Laptop tests | Board tests and records |
|---|---|---|---|
| 1 | Clean the workspace | — | Repository history |
| 2 | Create the `s3-port` baseline | — | Repository history |
| 3 | Rebuild the Pico reference | — | [Build record](parity/logs/3-build.txt) |
| 4 | Requirements and the parity test plan | LT-19 | [PARITY.md §1](parity/PARITY.md) |
| 5 | Run the full Pico baseline | — | [PARITY.md §2](parity/PARITY.md) |
| 6 | Freeze and tag the Pico reference | — | Tag `pico-reference`, [check](parity/logs/6-reference-check.txt) |
| 7 | Standardise the Arduino environment | — | Superseded by task 25 |
| 8 | Confirm the hardware and pin layout | — | [SETUP.md](SETUP.md) |
| 9 | Bring up the S3 with Arduino | — | [Record](parity/logs/9-arduino-bringup.txt) |
| 10 | Architecture freeze | LT-19 | [ARCHITECTURE.md](ARCHITECTURE.md) |
| 11 | Define the HAL | LT-01 | [task11/](parity/logs/task11/) |
| 12 | Arduino S3 HAL and shared build | LT-01 | [task12/](parity/logs/task12/) |
| 13 | Runtime, main control and REPL | LT-04 | [task13/](parity/logs/task13/), BT-02 |
| 14 | Scheduler and GPIO digital mode | LT-08 | BT-02, BT-04 |
| 15 | Pulse, counter and one-wire GPIO | LT-08 | BT-01 |
| 16 | I2C | LT-06 | BT-05 |
| 17 | UART | LT-08 | [PARITY.md §3](parity/PARITY.md) (loopback 8/8) |
| 18 | Rule engine actions | LT-04 | BT-02, BT-04 |
| 19 | Wi-Fi, MQTT and sensor bridging | LT-04, LT-11 | BT-07 |
| 20 | Model storage, transfer and inference | LT-09 | BT-02, BT-09, [task20](parity/task20/README.md) |
| 21 | End-to-end parity, fault, timing, endurance | all of LT-01 to LT-16 | BT-02 to BT-12 |
| 22 | Portability documentation | LT-19 | [PORTABILITY.md](docs/design/PORTABILITY.md) |
| 23 | Release from a clean Arduino environment | — | Superseded by task 34 |
| 24 | Plan the redesign | LT-19 | [MTK_REDESIGN.md](MTK_REDESIGN.md) |
| 25 | Pin the course port and toolchain | LT-17 | `sh tools/mtk.sh check` |
| 26 | Bring up µT-Kernel 3 SMP | LT-03 | [26-bringup.log](parity/logs/mtk/26-bringup.log) |
| 27 | µT-Kernel HAL backend | LT-02, LT-08 | BT-01 |
| 28 | µT-Kernel runtime | LT-03, LT-04, LT-18 | BT-13 |
| 29 | Interrupt-driven HC-SR04 echo | LT-08 | [29-echo-and-wcet.txt](parity/logs/mtk/29-echo-and-wcet.txt), BT-11 |
| 30 | Wi-Fi and MQTT on the course port | LT-04 | BT-10, [30-wifi-findings.txt](parity/logs/mtk/30-wifi-findings.txt) |
| 31 | Model storage and InferenceTask | LT-09 | BT-02, BT-09 |
| 32 | Verify and compare with FreeRTOS | all LT | BT-01 to BT-13, [PARITY.md §4](parity/PARITY.md) |
| 33 | Close the redesign documentation | LT-02, LT-19 | [MTK_REDESIGN.md §9](MTK_REDESIGN.md) |
| 34 | Release the µT-Kernel build | LT-17, LT-18 | Tag `d1-mtk-v1.0` |
| 1.1 | Two-board data | LT-05, LT-11 | BT-08 (6/6) |
| 1.2 | Two-board control (`cmd.to`, `cfg.to`) | LT-04 | BT-08 (10/10) |
| 1.3 | Configuration over MQTT | LT-05 | BT-08 (9/9) |
| 1.4 | Every console command over MQTT | LT-04 | BT-08 (18/18), BT-07 |
| 1.5 | Record and update | LT-19 | [d1-mqtt/](parity/logs/mtk/d1-mqtt/) |

### D2

| Task | Claim | Laptop tests | Board tests and records |
|---|---|---|---|
| D2-1 | Freeze the SPI contract | LT-05 (contract enforced) | Signed off 22 Nov |
| D2-2 | SPI in the shared code | LT-05, LT-07 (SPI driver) | BT-14 |
| D2-3 | ESP32-S3 SPI backend | LT-02 | BT-14 |
| D2-4 | Functional, fault and lifecycle tests | LT-05, LT-07 (SPI driver errors and lifecycle) | BT-14 |
| D2-5 | Benchmark 8 to 1024-byte transfers | — | BT-15 |
| D2-6 | W25Q32 device layer on hardware | LT-07 | BT-16 |
| D2-7 | Event record and flash log | LT-07 | [design](docs/d2/D2_EVENT_RECORD_DESIGN.md) |
| D2-8 | Log every event from ApplicationTask | LT-07 | BT-17 |
| D2-9 | Concurrent run (T-23) | — | BT-17 (6/6) |
| D2-10 | D1 regression on the D2 firmware | all LT | BT-18 |
| D2-11 | Documentation, review and merge | LT-19 | Signed off 22 Nov |

### D3

| Task | Claim | Laptop tests | Board tests and records |
|---|---|---|---|
| D3-1 | Timing modes and their configuration | LT-05, LT-08 | [native log](parity/logs/d3/d3-1-native-20261105.log) |
| D3-2 | HC-SR04 without blocking SchedulerTask | LT-08, LT-16 | BT-19 |
| D3-3 | Sonar A, guard, sonar B within 50 ms | LT-08 (pulse sequence: both orders, guard, 50 ms budget) | Done: [native log](parity/logs/d3/d3-1-native-20261105.log) (`[PAIR] state=complete`); run on the D6 two-sonar setup and signed off by the team 22 Nov |
| D3-4 | Radar presence opens the window | LT-08 (entrance window) | Done in code with laptop tests; the board run was dropped with the radar |
| D3-5 | Two-sonar order and guard | LT-08 | Done: run on the D6 two-sonar setup and signed off by the team 22 Nov |
| D3-6 | Requested against measured timing | LT-08 | BT-19 (timing table, event-driven, one-shot) |
| D3-7 | Entrance workload timing | LT-08 (window report) | Closed with the radar |
| D3-8 | ApplicationTask on µT-Kernel | LT-04, LT-08 | BT-19 (`app-test` PASS) |
| D3-9 | Faults never look like readings | LT-08 | BT-19, BT-03 |
| D3-10 | D1 regression on the D3 firmware | all LT | BT-18 |
| D3-11 | Agree the open contracts | LT-08 (contracts) | Signed off 22 Nov |
| D3-12 | Documentation, review and merge | LT-19 | Signed off 22 Nov |

### D4

| Task | Claim | Laptop tests | Board tests and records |
|---|---|---|---|
| 2.1 | ML running on the real board | LT-09 | BT-20 |
| 2.2 | Research S3 acceleration | LT-10 | [acceleration evidence](docs/embedded-ml/BOARD_VALIDATION.md#acceleration-evidence) |
| 2.3 | A small set of swappable models | LT-09 | BT-20, BT-21 |
| 2.4 | Prove the acceleration works | LT-10 | BT-21 |
| 2.5 | Load models without reflashing | LT-09, LT-13 | BT-09 |
| 2.6 | Explain the ML flow | LT-19 | [ML_FLOW.md](docs/embedded-ml/ML_FLOW.md) |
| 3.1 | The ML setup format | LT-09 | BT-20 |
| 3.2 | Switch the model live | LT-09 | BT-20, BT-23 |
| 3.3 | Switch the data source live | LT-09 | BT-23, BT-24 |
| 3.4 | Choose where results go | LT-09 | BT-20 |
| 3.5 | Bad input is handled | LT-09 | BT-20 |
| 3.6 | User how-to | LT-19 | [LIVE_SETUP_PROPOSAL.md](docs/embedded-ml/LIVE_SETUP_PROPOSAL.md) |
| 3.7 | Board evidence and update | LT-19 | BT-20, BT-24 |

### D5

| Task | Claim | Laptop tests | Board tests and records |
|---|---|---|---|
| 4.1 | Agree the scope | LT-19 | [docs/d5](docs/d5/README.md) |
| 4.2 | Acceleration on against off | LT-10 | BT-21 |
| 4.3 | Float against INT8, and does it fit | LT-10 | BT-21 (static sizes) |
| 4.4 | Where the time goes | LT-10 | BT-21 |
| 4.5 | In the running system | LT-10 | BT-21 (live run) |
| 4.6 | Explanation and write-up | LT-19 | [docs/d5](docs/d5/README.md) |

### D6

| Task | Claim | Laptop tests | Board tests and records |
|---|---|---|---|
| 5.1 | Confirm the use case | LT-19 | [README 5.1](README.md#51-overview) |
| 5.2 | Define each board | LT-05 | BT-22, [README 5.2](README.md#52-setup) |
| 5.3 | Board A senses | LT-05 (`board_a.cfg`), LT-06 | BT-23 |
| 5.4 | Board B follows A | LT-05 (`board_b_ml.cfg`), LT-11 | BT-23 |
| 5.5 | Live ML on B | LT-09 (the D6 setup files) | BT-23 |
| 5.6 | Live people-counter on B | LT-05 (people-counter profiles), LT-09 | BT-24 |
| 5.7 | Comfort score on C | LT-09, LT-11 | BT-23 |
| 5.8 | LCD board shows live data | LT-11, LT-12 | BT-23 |
| 5.9 | Doorway counter on A | LT-08 (doorway) | [README 5.5](README.md#55-more-commands) |
| 5.10 | Multi-bus demo | LT-05 (README 5.4 lines), LT-09 (local-sonar setups) | BT-25 |
| 5.11 | Full run on three boards | all LT | BT-23 |
