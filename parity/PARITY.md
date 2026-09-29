# D1 Requirements and Parity Table (Tasks 4 and 5)

Owner: Hasif. Requirements are proposed for team review (task 4). The Pico column is the executed baseline (task 5); rows marked pending need hardware or a network that was not available during the run. The ESP32-S3 column is filled in during tasks 13 to 21 using the same commands.

**Reference under test:** `s3-port` commit `543a759` (firmware sources unchanged through `23db9c8`), Raspberry Pi Pico 2 W (RP2350, confirmed from the bootloader's `INFO_UF2.TXT`), Pico SDK 2.3.0, Arm GNU Toolchain 14.2.Rel1. Tester: Hasif, 2026-09-28 to 2026-09-29. Build record and artifact hashes: [logs/3-build.txt](logs/3-build.txt).

**Reproducing a result:** the exact command text for every row is in [parity_test.py](parity_test.py) under the stage and step named in the table. Run `python3 parity/parity_test.py --stage <stage>` with the board connected over USB; the raw serial capture is written to `parity/logs/<stage>.log`.

## 1. Requirements (task 4, proposed)

### Functional

| ID | Requirement | Acceptance criterion | Planned test | Owner | Evidence |
|---|---|---|---|---|---|
| FR-01 | The REPL commands `HELP`, `SHOW`, `SHOWRULES`, `RUN`, `STOP`, `CLEAR` and `CLEARRULES` behave as on the Pico | Each command returns the same response lines as the Pico reference | P-01 to P-06 | Pico: Hasif. S3: task 13 | `4a.log` |
| FR-02 | An inline `BEGINCFG` block is parsed, validated and scheduled | `CFG: ok (added #n: NAME)` and the sensor appears in `SHOW` | P-07 | Pico: Hasif. S3: task 14 | `4a.log` |
| FR-03 | Invalid input is refused without disrupting the running system | Each case in N-01 to N-08 produces its documented message and the board keeps responding | N-01 to N-08 | Pico: Hasif. S3: tasks 13, 14, 18 | `5-neg.log` |
| FR-04 | GPIO digital input honours pull and reports both levels | Reads `00` and `01` when the pin is driven low and high | P-09, P-10 | Pico: Hasif. S3: task 14 | `4a.log`, `4b.log` |
| FR-05 | GPIO counter mode counts level changes | Count increases by one per change seen between samples | P-11 | Pico: Hasif. S3: task 15 | `4b.log` |
| FR-06 | GPIO pulse mode measures HC-SR04 echo width | Width changes with distance; no echo reports `len=1 : 00` | P-12, P-13 | Pico: Hasif. S3: task 15 | `4c-sonic.log` |
| FR-07 | GPIO onewire mode reads a DHT22 or AM2302 | Humidity and temperature frames decode to plausible values | P-14 | Pico: Hasif. S3: task 15 | Pending |
| FR-08 | I2C reads a register after optional pre-writes, with repeated start | A known device returns its identity register; a missing device is rejected | P-15, N-02 | Pico: Hasif. S3: task 16 | `5-neg.log` |
| FR-09 | UART request and response with pre-bytes, delay and read length | Bytes sent return intact and in order | P-16 | Pico: Hasif. S3: task 17 | `4c-uart.log` |
| FR-10 | Rule engine evaluates `calc`/`when` and runs every local action | `log`, GPIO `HIGH`/`LOW`/`TOGGLE`/`PULSE`, `pwm` and `batch:` actions run as documented | P-18 to P-22 | Pico: Hasif. S3: task 18 | `4a.log`, `4b.log` |
| FR-11 | Inference runs the built-in model through `infer.set` and `infer.run` | An `[INFER] invoke=` line with a score, mask and threshold is printed | P-23 | Pico: Hasif. S3: task 20 | `4a.log` |
| FR-12 | Wi-Fi connects and MQTT carries telemetry, text and JSON configuration, and commands | Telemetry reaches the broker; configuration and commands sent over MQTT are applied | P-25 to P-27 | Pico: Hasif. S3: task 19 | Pending |
| FR-13 | Remote rule actions (`cmd.to`, `cfg.to`) and sensor bridging work between nodes | A rule on one node changes the state of another | P-28 | Pico: Hasif. S3: task 19 | Pending |
| FR-14 | A model uploaded with `send_model.py` is stored and loaded without rebuilding firmware | Upload completes and `infer.run` uses the new model | P-29 | Pico: Hasif. S3: task 20 | Pending |
| FR-15 | Capacity is at least 8 sensors and 4 rules | The ninth sensor and fifth rule are refused with a message | N-04, N-06 | Pico: Hasif. S3: tasks 14, 18 | `5-neg.log` |

### Non-functional

Values in the target column are proposals for team review, derived from the Pico measurements.

| ID | Requirement | Pico measurement | Proposed ESP32-S3 target | Planned test |
|---|---|---|---|---|
| NFR-01 | Main loop and sampling period | Loop period 1000 to 1201 ms, mean 1006.4 ms over 301 passes. A sensor configured at `freq_hz=2` was sampled every 1002 to 1003 ms, because the main loop sleeps 1000 ms per pass | For `freq_hz=1`, period 1000 ms ±10%, with jitter recorded; never slower than the Pico | Heartbeat and sample timestamps in every log |
| NFR-02 | REPL response latency | Replies arrive within one loop pass on the board (at most about 1.2 s). USB delivery to the host occasionally lagged by up to about 5 s | Reply within 1 s of the command | Timestamped command and reply lines |
| NFR-03 | Inference latency | `invoke=` 961 to 1298 µs over 5 runs | No slower than 1298 µs | P-23 |
| NFR-04 | Inference agreement | `MSE=0.4179` for input 1.0 in slot 0 | Absolute difference of at most 0.01 for the same model and input | P-23 |
| NFR-05 | Stability | No resets or watchdog events across all runs; the board only rebooted when deliberately replugged or reflashed | Full parity suite plus an agreed continuous run with no resets | Every stage; continuous run to be agreed |
| NFR-06 | Resource use | Code 639,852 B, zero-initialised RAM 295,424 B | Fits the module confirmed in task 8, with the headroom recorded | Build size report |

### Reference defects found

These are behaviours of the supplied Pico firmware, recorded as the baseline. The team decides in task 10 whether the ESP32-S3 port reproduces each one for strict parity or fixes it.

| ID | Defect | Evidence |
|---|---|---|
| D-01 | An unsupported `proto=` value is not rejected. The parser keeps its default, so `proto=spi` becomes an I2C sensor on SDA 4, SCL 5, address `0x76`, and the reply is `CFG: ok` | N-01 |
| D-02 | A digital config without `gpio.pin` is accepted and silently reads GP2 | N-03 |
| D-03 | A malformed `when=` expression is accepted with `RULE: ok`; rules are only checked for a source and table space | N-07 |
| D-04 | A non-numeric pin in a GPIO action is accepted when the rule is added and only reported when it fires | N-05 |
| D-05 | The UART driver never empties its receive buffer. On the first run after boot, a stray `00` entered when the pins were set up, and every later reply stayed one byte out of step | P-17 |
| D-06 | Sampling is capped at about 1 Hz by the fixed 1000 ms main-loop delay, whatever `freq_hz` requests | NFR-01 |

## 2. Parity table (task 5)

**Status summary: task 5 is not complete, and no real sensor has been verified on the Pico yet.** Of 38 rows, 22 pass, but 16 of those need no wiring at all (REPL, configuration, rules, inference, invalid input and limits), and the other 6 used jumper-wire loopbacks with no sensor attached (GP21 to GP22 for GPIO, GP16 to GP17 for UART). One more row passes only on the timeout path, because the attached HC-SR04 never responded. Four defects, one quirk and one measurement are recorded. Nine rows are pending: every real sensor (HC-SR04, TSL2561 light sensor, one-wire, UART radar) and every Wi-Fi/MQTT row.

| ID | Req | Stage / step | Expected | Pico observed | Pico result | Evidence | ESP32-S3 result |
|---|---|---|---|---|---|---|---|
| P-01 | FR-01 | `4a` / `repl-help` | Command list | `Commands:` block | Pass | `4a.log` | |
| P-02 | FR-01 | `4a` / `repl-show`, `repl-show-cfg` | Sampling state, sensors and rule count | `[SHOW] sampling=0`, `[SCHED] #0 name=DIG …`, `[RULE] count=0` | Pass | `4a.log` | |
| P-03 | FR-01 | `4a` / `repl-run`, `repl-stop` | `RUN` / `STOP` echoed | `RUN`, `STOP` | Pass | `4a.log` | |
| P-04 | FR-01 | `4a` / `repl-showrules` | Rule count | `[RULE] count=2` | Pass | `4a.log` | |
| P-05 | FR-01 | `4a` / `repl-clearrules` | Rules cleared | `[RULE] reset` | Pass | `4a.log` | |
| P-06 | FR-01 | `4a` / `repl-clear` | Sensors and rules cleared | `CLEARED` | Pass | `4a.log` | |
| P-07 | FR-02 | `4a` / `cfg-digital` | Config accepted and scheduled | `CFG: ok (added #0: DIG)` | Pass | `4a.log` | |
| P-08 | FR-01 | `4b` / `boot` | Wi-Fi attempt, inference start, model load | `WiFi connect failed` (placeholder credentials), `Base model loaded: 16 inputs, 16 outputs`, 16 normalisation params, 16 threshold masks | Pass | `4b.log` | |
| P-09 | FR-04 | `4a` / `run-data` | Pin with pull-down reads low | `: 00 …` | Pass | `4a.log` | |
| P-10 | FR-04 | `4b` / `gpio-high`, `gpio-low`, `gpio-toggle` | GP22 follows GP21 through a jumper | `01` after HIGH, `00` after LOW, alternating on TOGGLE | Pass | `4b.log` | |
| P-11 | FR-05 | `4b` / `gpio-counter` | Count rises with each toggle | Reached `03 00 00 00` | Pass | `4b.log` | |
| P-12 | FR-06 | `4c-sonic` / `sonic-read` | Echo width changes with distance | No valid echo: the HC-SR04 was powered at 3.3 V and needs 5 V with a 1:2 resistor divider on Echo | **Pending hardware** | `4c-sonic.log` | |
| P-13 | FR-06 | `4c-sonic` / `sonic-read` | No echo reports a timeout frame | `len=1 : 00` on 44 of 44 readings | Pass | `4c-sonic.log` | |
| P-14 | FR-07 | Not yet written | DHT22 or AM2302 frame | No sensor of this type available | **Pending hardware** | | |
| P-15 | FR-08 | `4c-i2c` / `cfg-tsl-id`, `i2c-id` | TSL2561 identity register reads `0x5X` | Device did not acknowledge at `0x29`, `0x39` or `0x49`; wiring to be rechecked | **Pending hardware** | `4c-i2c.log` | |
| P-16 | FR-09 | `4c-uart` / `uart-echo-a`, `uart-echo-b` | Bytes return intact and in order | `A5 5A` and `12 34 56` with GP16 looped to GP17 | Pass | `4c-uart.log` | |
| P-17 | FR-09 | `4c-uart` / `uart-echo-a` | First read after boot aligned | Offset by one byte on the first run after boot (D-05) | Recorded quirk | Observed 2026-09-29; that log was replaced by the passing rerun | |
| P-18 | FR-10 | `4a` / `rule-log`, `run-data` | `log` action fires when `when` is true | `[RULE] R_LOG: pin low (value=0.000 …)` | Pass | `4a.log` | |
| P-19 | FR-10 | `4b` / `gpio-high`, `gpio-low`, `gpio-toggle` | GPIO actions change the pin | Confirmed electrically on GP22 | Pass | `4b.log` | |
| P-20 | FR-10 | `4b` / `gpio-pulse` | `PULSE` action runs | `gpio 21=PULSE 200ms`; pulse length not measured | Pass | `4b.log` | |
| P-21 | FR-10 | `4b` / `pwm-run` | PWM duty set | `pwm 21 duty=0.500`; waveform not measured | Pass | `4b.log` | |
| P-22 | FR-10 | `4a` / `rule-infer` | `batch:` with escaped `\|` accepted and run | `RULE: ok`, then inference ran | Pass | `4a.log` | |
| P-23 | FR-11 | `4a` / `run-data` | Inference prints latency, score, mask, threshold | `invoke=1298us`, `MSE=0.4179`, `mask=1000`, `thresh=1.1407 OK` | Pass | `4a.log` | |
| P-24 | NFR-01 | All stages | Loop period and sample interval recorded | 1000 to 1201 ms; `freq_hz=2` sampled at about 1 Hz | Measured | All logs | |
| P-25 | FR-12 | Not yet written | Wi-Fi connect with real credentials | Not run | **Pending network** | | |
| P-26 | FR-12 | Not yet written | Telemetry on `pico/<node>/sensor/<name>` | Not run | **Pending network** | | |
| P-27 | FR-12 | Not yet written | Text and JSON configuration and commands over MQTT | Not run | **Pending network** | | |
| P-28 | FR-13 | Not yet written | `cmd.to`, `cfg.to` and bridging between two nodes | Not run; needs a second Pico | **Pending network** | | |
| P-29 | FR-14 | Not yet written | `send_model.py` upload, then `infer.run` | Not run | **Pending network** | | |
| P-30 | FR-09 | Not yet written | The team's reference UART sensor (radar) returns its frames | Not run. UART has only been proven with a loopback wire (P-16) | **Pending hardware** | | |
| N-01 | FR-03 | `5-neg` / `neg-proto` | Unsupported protocol refused | Accepted as I2C on default pins (D-01) | Defect recorded | `5-neg.log` | |
| N-02 | FR-08 | `5-neg` / `neg-i2c-nodev` | Missing I2C device refused | `init_cfg FAILED for NODEV`, `CFG: add failed (-2)` | Pass | `5-neg.log` | |
| N-03 | FR-03 | `5-neg` / `neg-nopin` | Config without a pin refused | Accepted on GP2 (D-02) | Defect recorded | `5-neg.log` | |
| N-04 | FR-15 | `5-neg` / `slot-1` to `slot-8`, `neg-slots` | Eight sensors accepted, ninth refused | 8 × `CFG: ok`, then `[SCHED] no free slots` | Pass | `5-neg.log` | |
| N-05 | FR-03 | `5-neg` / `rule-4-badpin`, `neg-badpin-run` | Non-numeric GPIO pin refused | Accepted, then reported when fired: `bad gpio pin in action='gpio:X=HIGH'` (D-04) | Defect recorded | `5-neg.log` | |
| N-06 | FR-15 | `5-neg` / `rule-1` to `rule-4-badpin`, `neg-rule-full` | Four rules accepted, fifth refused | `[RULE] table full`, `RULE: parse error` | Pass | `5-neg.log` | |
| N-07 | FR-03 | `5-neg` / `neg-badexpr` | Malformed expression refused | `RULE: ok` (D-03) | Defect recorded | `5-neg.log` | |
| N-08 | FR-03 | `5-neg` / `neg-rule-nosource`, `neg-toolong`; `4a` / `repl-unknown` | Refused with a message | `[RULE] missing source=`, `Line too long`, `Unknown: FOO` | Pass | `5-neg.log`, `4a.log` | |
