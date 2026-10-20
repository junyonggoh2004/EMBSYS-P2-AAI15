# D1 Requirements and Parity Table with Proposed D2 SPI Extension

Owner: Hasif. Requirements signed off by the team (task 4). The Pico column is the executed baseline (task 5); rows marked pending need hardware or a network that was not available during the run. The ESP32-S3 column is filled in during tasks 13 to 21 using the same commands.

D2 additions FR-16, FR-17, NFR-08 and T-17 to T-23 are proposed by Brendan and Jy on branch `s3-port-d2`. They are not part of the signed-off D1/Pico parity baseline until the team reviews them. D2 is ESP32-S3-specific, so its Pico result is N/A rather than an invented parity result.

**Reference under test:** `s3-port` commit `543a759` (firmware sources unchanged through `23db9c8`), Raspberry Pi Pico 2 W (RP2350, confirmed from the bootloader's `INFO_UF2.TXT`), Pico SDK 2.3.0, Arm GNU Toolchain 14.2.Rel1. Tester: Hasif, 2026-09-28 to 2026-10-02. The Wi-Fi and MQTT rows used the same sources with a home network's credentials and the broker address in the gitignored `project_v3/include/wifi_credentials.h`. Build record and artifact hashes: [logs/3-build.txt](logs/3-build.txt). Frozen as the annotated tag `pico-reference` (task 6); the clean-checkout rebuild check is in [logs/6-reference-check.txt](logs/6-reference-check.txt).

**Reproducing a result:** the exact command text for every row is in [parity_test.py](parity_test.py) under the stage and step named in the table. Run `python3 parity/parity_test.py --stage <stage>` with the board connected over USB; the raw serial capture is written to `parity/logs/<stage>.log`. The Wi-Fi and MQTT rows (P-25 to P-29) use [mqtt_test.py](mqtt_test.py) instead: run a Mosquitto broker on the host, install `paho-mqtt`, set `MQTT_HOST` in `wifi_credentials.h` to the host's address, then run `python3 parity/mqtt_test.py --broker <host address> --boot` and reflash or replug the board. It sends every command over MQTT, checks the serial log and the broker traffic together, and writes `parity/logs/mqtt.log`. The host also plays a second node, `pico-002`, so P-28 needs only one board. The Pico 2 W joins 2.4 GHz WPA2-Personal networks only.

## 1. Requirements (task 4)

### Functional

| ID | Requirement | Acceptance criterion | Planned test | Owner | Evidence |
|---|---|---|---|---|---|
| FR-01 | The REPL commands `HELP`, `SHOW`, `SHOWRULES`, `RUN`, `STOP`, `CLEAR` and `CLEARRULES` behave as on the Pico | Each command returns the same response lines as the Pico reference | P-01 to P-06 | Pico: Hasif. S3: task 13 | `4a.log` |
| FR-02 | An inline `BEGINCFG` block is parsed, validated and scheduled | `CFG: ok (added #n: NAME)` and the sensor appears in `SHOW` | P-07 | Pico: Hasif. S3: task 14 | `4a.log` |
| FR-03 | Invalid input is refused without disrupting the running system | Each case in N-01 to N-08 produces its documented message and the board keeps responding | N-01 to N-08 | Pico: Hasif. S3: tasks 13, 14, 18 | `5-neg.log` |
| FR-04 | GPIO digital input honours pull and reports both levels | Reads `00` and `01` when the pin is driven low and high | P-09, P-10 | Pico: Hasif. S3: task 14 | `4a.log`, `4b.log` |
| FR-05 | GPIO counter mode counts level changes | Count increases by one per change seen between samples | P-11 | Pico: Hasif. S3: task 15 | `4b.log` |
| FR-06 | GPIO pulse mode measures HC-SR04 echo width | Width changes with distance; no echo reports `len=1 : 00` | P-12, P-13 | Pico: Hasif. S3: task 15 | `4c-sonic.log` |
| FR-07 | GPIO onewire mode reads a DHT22 or AM2302 | Humidity and temperature frames decode to plausible values | P-14 | Pico: Hasif. S3: task 15 | Not testable (no one-wire sensor in kit) |
| FR-08 | I2C reads a register after optional pre-writes, with repeated start | A known device returns its identity register; a missing device is rejected | P-15, N-02 | Pico: Hasif. S3: task 16 | `4c-i2c.log`, `5-neg.log` |
| FR-09 | UART request and response with pre-bytes, delay and read length | Bytes sent return intact and in order | P-16 | Pico: Hasif. S3: task 17 | `4c-uart.log` |
| FR-10 | Rule engine evaluates `calc`/`when` and runs every local action | `log`, GPIO `HIGH`/`LOW`/`TOGGLE`/`PULSE`, `pwm` and `batch:` actions run as documented | P-18 to P-22 | Pico: Hasif. S3: task 18 | `4a.log`, `4b.log` |
| FR-11 | Inference runs the built-in model through `infer.set` and `infer.run` | An `[INFER] invoke=` line with a score, mask and threshold is printed | P-23 | Pico: Hasif. S3: task 20 | `4a.log` |
| FR-12 | Wi-Fi connects and MQTT carries telemetry, text and JSON configuration, and commands | Telemetry reaches the broker; configuration and commands sent over MQTT are applied | P-25 to P-27 | Pico: Hasif. S3: task 19 | `mqtt.log`, `mqtt-disconnect.log` |
| FR-13 | Remote rule actions (`cmd.to`, `cfg.to`) and sensor bridging work between nodes | A rule on one node changes the state of another | P-28 | Pico: Hasif. S3: task 19 | `mqtt.log`, `mqtt-cfgto-receive.log`; fails in the reference (D-10, D-11) |
| FR-14 | A model uploaded with `send_model.py` is stored and loaded without rebuilding firmware | Upload completes and `infer.run` uses the new model | P-29 | Pico: Hasif. S3: task 20 | `mqtt.log` |
| FR-15 | Capacity is at least 8 sensors and 4 rules | The ninth sensor and fifth rule are refused with a message | N-04, N-06 | Pico: Hasif. S3: tasks 14, 18 | `5-neg.log` |
| FR-16 (D2) | SPI is a supported protocol behind the common bus registry and runtime configuration path | `proto=spi` resolves to `bus_spi`; host, SCLK, MOSI, MISO, CS, frequency, mode, TX/RX sizes and timeout are validated and shown; a known-pattern transaction returns the expected bytes | T-17 to T-20 | S3: Brendan and Jy | T-17–19 loopback pass; W25Q32 T-20 pending. See `logs/d2-spi/RESULTS.md` |
| FR-17 (D2) | SPI transaction and lifecycle failures are explicit and cannot appear as valid sensor samples | Invalid/conflicting settings, timeout, I/O failure, failed reconfiguration and device-verification failures return explicit errors; partial resources are released and the previous working device survives failed reconfiguration | T-21 to T-23 | S3: Brendan and Jy | Config, timeout, lock and lifecycle cases pass; removed MISO produced published raw bytes, so absent-device verification is open. See `logs/d2-spi/RESULTS.md` |

### Non-functional

Targets are derived from the Pico measurements and signed off by the team.

| ID | Requirement | Pico measurement | ESP32-S3 target | Planned test |
|---|---|---|---|---|
| NFR-01 | Main loop and sampling period | Loop period 1000 to 1201 ms, mean 1006.4 ms over 301 passes. A sensor configured at `freq_hz=2` was sampled every 1002 to 1003 ms, because the main loop sleeps 1000 ms per pass | For `freq_hz=1`, period 1000 ms ±10%, with jitter recorded; never slower than the Pico | Heartbeat and sample timestamps in every log |
| NFR-02 | REPL response latency | Replies arrive within one loop pass on the board (at most about 1.2 s). USB delivery to the host occasionally lagged by up to about 5 s | Reply within 1 s of the command | Timestamped command and reply lines |
| NFR-03 | Inference latency | `invoke=` 961 to 1298 µs over 5 runs | No slower than 1298 µs | P-23 |
| NFR-04 | Inference agreement | `MSE=0.4179` for input 1.0 in slot 0 | Absolute difference of at most 0.01 for the same model and input | P-23 |
| NFR-05 | Stability | No resets or watchdog events across all runs; the board only rebooted when deliberately replugged or reflashed | Full parity suite plus an agreed continuous run with no resets | Every stage; continuous run to be agreed |
| NFR-06 | Resource use | Code 639,852 B, zero-initialised RAM 295,424 B | Fits the module confirmed in task 8, with the headroom recorded | Build size report |
| NFR-08 (D2) | SPI integrity and measured performance | N/A; D2 has no Pico baseline requirement | Payloads 8–1024 bytes have zero byte mismatches in loopback; median, p95, p99 and maximum latency, effective throughput, transaction error rate, byte error rate and heap delta are recorded; approved numerical thresholds and the readable-device/concurrent run remain for sign-off | T-19, T-22, T-23; five loopback configurations and 40,000 transfers passed in `logs/d2-spi/` |

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
| D-07 | With nothing in range, the HC-SR04 holds Echo high for longer than `gpio.pulse_timeout_us`. The driver clips the width at the timeout but still marks the reading valid: `A9 61 01` is 25 001 µs, about 431 cm. "Nothing in range" is therefore indistinguishable from a real reading at the timeout distance | P-12, 26 readings in `4c-sonic.log` |
| D-08 | A GPIO pin number that does not exist (`gpio.pin=99`; the RP2350 has GPIO 0 to 29) is accepted, and the sensor then publishes normal-looking `00` readings from it. Wrong data with no error | T-03 |
| D-09 | At boot the firmware waits only 10 s for Wi-Fi, then prints `WiFi connect failed`, but the join carries on in the background. On the test network it finished a few seconds later and MQTT connected about 15 s after boot, so the boot message is wrong and only the periodic `[MQTT] Status:` line shows the late success | P-25, `mqtt.log` boot capture |
| D-10 | Sensor bridging never delivers. A `proto=mqtt` sensor subscribes to the remote node's line topic, but the MQTT receive handler discards every topic containing `/sensor/` (intended to drop the node's own telemetry echo) before the bridge routing runs, so a remote line is dropped silently (`project_v3/mqtt/mqtt_telemetry.c`, the early return in `mqtt_incoming_data_cb`) | P-28 |
| D-11 | `cfg.to` sends, but the receiving node cannot apply what it receives. Inside a text rule the block's separators must be escaped as `\|` (the unescaped form in the project README is split by the rule parser), and the escapes are forwarded unchanged: a node given `BEGINCFG\|name=LED\|proto=gpio\|…` added an I2C sensor named `LED\`, because the unrecognised protocol falls back to I2C (D-01). The unquoted JSON form used in `test_json/h206_remote.json` is forwarded as is and rejected by the receiver (`jsmn error=-2`, `unrecognized payload`) | P-28, `mqtt-cfgto-receive.log` |
| D-12 | The heartbeat's `uptime_s` field counts heartbeats, one every 10 s, not seconds: it read 12 a little over two minutes after boot | P-25, `mqtt.log` |
| D-13 (open, not yet attributed) | The board stopped responding on both USB and Wi-Fi about 28 minutes after boot while idle and connected to the broker. Seen once, on 2026-10-04, on the task 11 build; a replug recovered it. The MQTT code calls lwIP from the main loop without `cyw43_arch_lwip_begin`/`end`, which `pico_cyw43_arch_lwip_threadsafe_background` requires, and task 11 did not change that code. The original firmware ran 35 minutes without a hang on 2026-10-02, so this is not yet confirmed as a reference defect. Check pending: the same flood test on both builds on the home network | NFR-05 |
| D-14 | A `batch:` or chained `infer.run:` rule action copies the whole rule (`rule_t`, about 17 KB) onto the stack for each sub-action. The Pico's main stack is 2 KB, so the copy writes about 15 KB below it into the top of RAM with no error; on the ESP32-S3 the stack guard caught it and reset the board during stage `4a`. Possibly related to D-13. Fixed in task 18 by passing the sub-action text instead of a copy | P-22, `task13/s3-4a-crash.txt` |
| D-15 | ESP32-S3 only. About one boot in 6 to 15, CPU1 stops in the FreeRTOS context switch while reading the system timer, about 1–2 s after power-up while Wi-Fi starts, and the interrupt watchdog reboots the board (`Guru Meditation Error: Core 1 panic'ed (Interrupt wdt timeout on CPU1)`). The next boot runs normally; no panic occurred after start-up in any test, including the 30-minute run. Turning off Wi-Fi modem sleep and Wi-Fi flash writes reduced it (1 in 6 → 1 in 15 boots) but did not remove it. It hit the first boot after flashing in 3 of 3 cases seen by the tester, against 0 of 8 resets with the console attached. Open: next step is to boot without Wi-Fi to isolate it |  |

### Planned test rows for tasks 13 to 20

The README's acceptance for tasks 13 to 20 names specific fault and limit cases. Each has a row here so that every planned feature has a Pico and an ESP32-S3 test. Rows marked N/A on the Pico have no Pico equivalent, because the Pico firmware runs a single bare-metal loop with no RTOS.

| ID | Task | Planned test | Pico status | ESP32-S3 result |
|---|---|---|---|---|
| T-01 | 13 | FreeRTOS tasks run the REPL and scheduler with no watchdog resets; stack high-water marks recorded for every task | N/A (no RTOS) | |
| T-02 | 14 | `gpio.invert=1` and `gpio.debounce_ms` change the reported state as documented | Invert: pass. On 81 paired samples of the same pin, the `gpio.invert=1` sensor always reported the opposite of the plain one (stage `4d-ir`). Debounce: not measurable at the reference's 1 s sampling interval (D-06) | |
| T-03 | 14 | An out-of-range `gpio.pin` is refused rather than crashing the board | Defect recorded (D-08): `gpio.pin=99` is accepted with `CFG: ok` and then publishes normal-looking `00` readings every second. The board does not crash. Stage `t03`, `logs/t03.log` | |
| T-04 | 14 | Eight sensors at the highest supported rate keep their sample period, or the overload is reported | Pass on stability, with a gap: 8 sensors at a requested `freq_hz=20` (50 ms) all kept publishing and the board kept responding, but every sensor was sampled every 1002 to 1003 ms and no overload was reported (D-06). Stage `t04`, `logs/t04.log` | |
| T-05 | 15 | Counter mode loses no edges up to a stated maximum rate | Not yet run; the maximum rate is to be stated in task 10 | |
| T-06 | 15 | One-wire with the sensor absent times out cleanly | Not yet run | |
| T-07 | 16 | I2C with `i2c.restart=0` against `1`, two devices on one bus, and recovery after a device is unplugged | Not yet run; the TSL2561 is now available for it | |
| T-08 | 17 | UART `uart.line_mode=1`, framing settings (`uart.bits`, `uart.parity`, `uart.stop`), a partial message, and more input than `uart.read_len` | Not yet run; possible with the loopback wire | |
| T-09 | 17 | UART under sustained input for an agreed period with no lost or corrupted frames | Not yet run | |
| T-10 | 18 | A `when=` condition that is false runs no action | Pass: over 6 s the false rule never fired while a true control rule on the same sensor did (stage `t10`, `logs/t10.log`) | |
| T-11 | 19 | Recovery from Wi-Fi loss and broker loss, then reconnection | Broker loss: pass. With the broker stopped the board logged `conn status=256` within a second, kept retrying every 5 s, and on restart reconnected within 3 s, resubscribed, republished its presence and resumed heartbeats (`mqtt-disconnect.log`). Wi-Fi loss: not yet run | |
| T-12 | 19 | Malformed JSON and an oversized MQTT payload are refused without a reset | Not yet run; the P-25 to P-29 setup can host it | |
| T-13 | 19 | Messages arriving faster than they are processed (queue saturation) are handled without a reset | Not yet run; the P-25 to P-29 setup can host it | |
| T-14 | 20 | An uploaded model persists across a reboot | Not yet run; the P-25 to P-29 setup can host it | |
| T-15 | 20 | Invalid, oversized, corrupt and interrupted uploads are rejected and the previous model still works | Not yet run; the P-25 to P-29 setup can host it | |
| T-16 | 20 | Tensor arena and heap use recorded during inference | Not yet run | |
| T-17 | D2 | A normal `proto=spi` configuration resolves through `bus_lookup("spi")`, is shown by the scheduler, and initializes through the common driver path | N/A (D2 ESP32-S3 extension) | Pass: `logs/d2-spi/spi.log` |
| T-18 | D2 | Runtime host, SCLK, MOSI, MISO, CS, clock and mode changes are validated; compatible devices share a bus and incompatible re-use is rejected | N/A (D2 ESP32-S3 extension) | Pass: `logs/d2-spi/spi.log`, `logs/d2-spi/fault-connected.log` |
| T-19 | D2 | Known-pattern TX/RX integrity passes for 8, 16, 32, 64, 128, 256, 512 and 1024-byte payloads | N/A (D2 ESP32-S3 extension) | Pass: five configurations, 40,000 transfers; `logs/d2-spi/bench-20261017-*/` |
| T-20 | D2 | A W25Q32 is identified and supports verified read/write plus retrievable event records through the generic SPI transport and a device layer | N/A (D2 ESP32-S3 extension) | Pending module; TFT is optional, not equivalent |
| T-21 | D2 | Invalid host/mode/clock/pins, duplicate/conflicting pins, missing MISO for RX, oversized transfers, timeout and absent/disconnected W25Q32 are explicit errors and publish no valid-looking sample | N/A (D2 ESP32-S3 extension) | Config/clock/timeout pass; absent-device check open after disconnected loopback published unverified bytes (`logs/d2-spi/RESULTS.md`) |
| T-22 | D2 | One hundred initialize/deinitialize or reconfiguration cycles have no progressive heap loss; failed reconfiguration leaves the previous device operational | N/A (D2 ESP32-S3 extension) | Pass: 100 cycles, heap 95,748 B before/after; `logs/d2-spi/spi.log` |
| T-23 | D2 | SPI latency/throughput/error benchmark and concurrent SPI plus existing sensor/inference workload complete without resets or unexplained corruption | N/A (D2 ESP32-S3 extension) | Loopback benchmark pass only; concurrent flash + live sensor + inference pending |

## 2. Parity table (task 5)

**Status summary: task 5 is complete. Every feature has been run on the Pico; the UART feature is verified with a loopback wire (P-16), and the team's UART radar (P-30) was not at hand and is recorded as deferred. Two real sensors are verified: the TSL2561 light sensor over I2C (P-15) and the HC-SR04 ultrasonic sensor in GPIO pulse mode (P-12).** Of 38 rows, 29 pass: 16 need no wiring at all (REPL, configuration, rules, inference, invalid input and limits), 6 used jumper-wire loopbacks with no sensor attached (GP21 to GP22 for GPIO, GP16 to GP17 for UART), 2 are the real sensors, 1 is the HC-SR04's no-echo timeout path, and 4 are Wi-Fi/MQTT rows run on a real 2.4 GHz home network with a Mosquitto broker on the host. P-28 passes only in part: `cmd.to` works, but `cfg.to` (D-11) and sensor bridging (D-10) do not work in the reference. Twelve reference defects are recorded (D-01 to D-12), with one quirk (P-17) and one measurement (P-24). One-wire is not testable because the kit has no one-wire sensor (P-14). Deferred: the UART radar (P-30), to be added when it is at hand; it does not block the `pico-reference` tag because UART itself is verified.

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
| P-12 | FR-06 | `4c-sonic` / `sonic-read` | Echo width changes with distance | 18 in-range readings from 5.9 cm to 30.9 cm, following an object moved between about 6 cm and 30 cm. Wiring: VCC to **VBUS (5 V)**, Trig to GP16, Echo through one series resistor to GP17, GND. This works because RP2350 GPIO inputs tolerate up to 5.5 V while the chip is powered. **ESP32-S3 GPIO pins are not 5 V tolerant, so the S3 wiring must use a 1:2 divider or a level shifter on Echo.** At a 3.3 V supply this sensor never echoed | Pass | `4c-sonic.log` | |
| P-13 | FR-06 | `4c-sonic` / `sonic-read` | No echo reports a timeout frame | With a 3.3 V supply the sensor never raised Echo, and all 44 readings were `len=1 : 00`. That run's log was replaced by the 5 V run. At 5 V with nothing in range, see D-07 | Pass | Observed 2026-09-29 | |
| P-14 | FR-07 | Not yet written | DHT22 or AM2302 frame | No one-wire sensor is in the team's kit. Hasif confirmed the kit's temperature sensor is the 4-pin LM393 comparator board (P-32), which has digital and analog outputs, not DHT one-wire framing. Disposition: untestable with current hardware; a DHT22 or AM2302 must be sourced if one-wire parity is required (owner: Hasif) | **Not testable (no one-wire sensor in kit)** | | |
| P-15 | FR-08 | `4c-i2c` / `cfg-tsl-id`, `i2c-id`, `i2c-light` | TSL2561 identity register reads `0x5X`; light channels respond to light | ID `0x50` at address `0x29`. Channel 0 read 18 to 20 in room light and 69 to 77 under a torch; channel 1 read 3, rising to 9 to 11. On this Grove cable the **yellow wire carries SDA (GP16) and the white wire SCL (GP17)**, the reverse of the usual Grove colours | Pass | `4c-i2c.log` | |
| P-16 | FR-09 | `4c-uart` / `uart-echo-a`, `uart-echo-b` | Bytes return intact and in order | `A5 5A` and `12 34 56` with GP16 looped to GP17 | Pass | `4c-uart.log` | |
| P-17 | FR-09 | `4c-uart` / `uart-echo-a` | First read after boot aligned | Offset by one byte on the first run after boot (D-05) | Recorded quirk | Observed 2026-09-29; that log was replaced by the passing rerun | |
| P-18 | FR-10 | `4a` / `rule-log`, `run-data` | `log` action fires when `when` is true | `[RULE] R_LOG: pin low (value=0.000 …)` | Pass | `4a.log` | |
| P-19 | FR-10 | `4b` / `gpio-high`, `gpio-low`, `gpio-toggle` | GPIO actions change the pin | Confirmed electrically on GP22 | Pass | `4b.log` | |
| P-20 | FR-10 | `4b` / `gpio-pulse` | `PULSE` action runs | `gpio 21=PULSE 200ms`; pulse length not measured | Pass | `4b.log` | |
| P-21 | FR-10 | `4b` / `pwm-run` | PWM duty set | `pwm 21 duty=0.500`; waveform not measured | Pass | `4b.log` | |
| P-22 | FR-10 | `4a` / `rule-infer` | `batch:` with escaped `\|` accepted and run | `RULE: ok`, then inference ran | Pass | `4a.log` | |
| P-23 | FR-11 | `4a` / `run-data` | Inference prints latency, score, mask, threshold | `invoke=1298us`, `MSE=0.4179`, `mask=1000`, `thresh=1.1407 OK` | Pass | `4a.log` | |
| P-24 | NFR-01 | All stages | Loop period and sample interval recorded | 1000 to 1201 ms; `freq_hz=2` sampled at about 1 Hz | Measured | All logs | |
| P-25 | FR-12 | `mqtt_test.py` / `boot`, `connect` | Wi-Fi joins, the board connects to the broker, subscribes, announces itself and sends heartbeats | Joined a 2.4 GHz WPA2-Personal network; `[MQTT] connected to broker`, `Subscribed OK to: node-wildcard`, retained `pico/pico-001/presence {"node":"pico-001","cap":[…]}`, `pico/pico-001/heartbeat` every 10 s. The boot line still says `WiFi connect failed` (D-09), and `uptime_s` counts heartbeats (D-12). Broker loss and recovery: see T-11 | Pass (with D-09, D-12) | `mqtt.log`, `mqtt-disconnect.log` | |
| P-26 | FR-12 | `mqtt_test.py` / `cmd-run` | Telemetry and rule events reach the broker | `pico/pico-001/sensor/DIG/line proto=gpio src=DIG len=3 ts=33840 : 00 8C 13` matching the serial line, and `pico/rule/R_LOG/event … type=log value=0.000 msg=pin low`. The JSON form with `bytes_b64` that the project README describes is never sent: its publisher (`mqtt_pub_json_sensor`) has no callers and `mqtt_pub_bytes` is an empty stub, so only the `/line` form is published | Pass | `mqtt.log` | |
| P-27 | FR-12 | `mqtt_test.py` / `cfg-text` to `cmd-clear-2` | Text and JSON configuration and the commands `RUN`, `STOP`, `SHOW`, `CLEAR` work over MQTT; an unknown command is refused | Text config and rule acknowledged on `status/config OK:DIG` and `status/rule OK:R_LOG`; a JSON payload with one sensor and one rule converted (`-> 186`), applied, acknowledged and fired; `[CMD] RUN`, `STOP`, `SHOW`, `CLEAR` acted on; `FOO` gave `[CMD] Unknown: FOO` | Pass | `mqtt.log` | |
| P-28 | FR-13 | `mqtt_test.py` / `to-run`, `bridge-cfg`, `bridge-line`; follow-up in `mqtt-cfgto-receive.log` | `cmd.to` and `cfg.to` reach another node and take effect; a remote sensor line is bridged into a local sensor. The host played `pico-002` | `cmd.to`: `pico/pico-002/cmd RUN` received, pass. `cfg.to`: the block reached `pico/pico-002/config`, but neither payload form is applied correctly by a receiving node (D-11). Bridging: the `proto=mqtt` sensor was accepted (`added MQTT-remote: local=REM <- pico-002/LIGHT`), but a line published on `pico/pico-002/sensor/LIGHT/line` never arrived (D-10) | Partial: `cmd.to` pass; `cfg.to` and bridging defects recorded | `mqtt.log`, `mqtt-cfgto-receive.log` | |
| P-29 | FR-14 | `mqtt_test.py` / `model-upload` to `infer-run` | `send_model.py` uploads the model and parameters, the board verifies and loads them, and inference runs on the new model | `Transfer starting: 16440 bytes, CRC32=0xF6AED6F8`, `Transfer complete! 16440 bytes verified at 0x103C0000`, `Auto-load successful`, `Model loaded: 16 inputs, 16 outputs`, `Loaded 16 normalization params`; then `invoke=1287us | MSE=0.4179 mask=1000 thresh=1.1407 OK`. The uploaded file is the same autoencoder as the built-in one, so the score matches P-23 | Pass | `mqtt.log` | |
| P-30 | FR-09 | Not yet written | The team's reference UART sensor (radar) returns its frames | Not run: the radar was not at hand during the baseline. The UART feature itself is verified with GP16 looped to GP17 (P-16). Disposition: deferred; run it with the same `4c-uart` settings and the radar's own frame format when the radar is available (owner: Hasif) | **Deferred (UART verified by loopback, P-16)** | | |
| P-31 | FR-04, FR-05 | `4d-ir` / `ir-run` | A real digital sensor (3-pin IR module, OUT on GP16) switches the input between `00` and `01`, and counter mode counts the changes | Not verified. The module's own indicator LED visibly changed when triggered, but GP16 read `01` on all 81 samples across two runs and the counter stayed at 0. The cause is not identified; a wiring fault at GP16 is suspected. A pull-down scan read GP16 and GP17 both high, possibly the RP2350's pull-down latching erratum (E9) | Not verified; **optional extra, not required for task 5** (GPIO digital and counter are covered by P-10 and P-11) | `4d-ir.log` | |
| P-32 | FR-04, FR-05 | `4e-comp` / `comp-run` | The team's temperature sensor, confirmed by Hasif to be a 4-pin LM393 comparator board (D0 on GP18), flips D0 when its threshold knob is turned | Not verified. D0 was actively driven high (it read `01` with the pull-down enabled, so the wire was connected), but it stayed `01` on all 40 samples while the knob was turned, and the counter stayed at 0. The board's sensing element appears to be missing (empty pads), so its threshold may never be crossed. This board is not a one-wire sensor, so it cannot cover P-14 | Not verified; **optional extra, not required for task 5** | `4e-comp.log` | |
| N-01 | FR-03 | `5-neg` / `neg-proto` | Unsupported protocol refused | Accepted as I2C on default pins (D-01) | Defect recorded | `5-neg.log` | |
| N-02 | FR-08 | `5-neg` / `neg-i2c-nodev` | Missing I2C device refused | `init_cfg FAILED for NODEV`, `CFG: add failed (-2)` | Pass | `5-neg.log` | |
| N-03 | FR-03 | `5-neg` / `neg-nopin` | Config without a pin refused | Accepted on GP2 (D-02) | Defect recorded | `5-neg.log` | |
| N-04 | FR-15 | `5-neg` / `slot-1` to `slot-8`, `neg-slots` | Eight sensors accepted, ninth refused | 8 × `CFG: ok`, then `[SCHED] no free slots` | Pass | `5-neg.log` | |
| N-05 | FR-03 | `5-neg` / `rule-4-badpin`, `neg-badpin-run` | Non-numeric GPIO pin refused | Accepted, then reported when fired: `bad gpio pin in action='gpio:X=HIGH'` (D-04) | Defect recorded | `5-neg.log` | |
| N-06 | FR-15 | `5-neg` / `rule-1` to `rule-4-badpin`, `neg-rule-full` | Four rules accepted, fifth refused | `[RULE] table full`, `RULE: parse error` | Pass | `5-neg.log` | |
| N-07 | FR-03 | `5-neg` / `neg-badexpr` | Malformed expression refused | `RULE: ok` (D-03) | Defect recorded | `5-neg.log` | |
| N-08 | FR-03 | `5-neg` / `neg-rule-nosource`, `neg-toolong`; `4a` / `repl-unknown` | Refused with a message | `[RULE] missing source=`, `Line too long`, `Unknown: FOO` | Pass | `5-neg.log`, `4a.log` | |

## 3. ESP32-S3 results (tasks 13 to 21)

Final build on 2026-10-13: HW-747 Super Mini (ESP32-S3, 4 MB flash, 2 MB PSRAM), core 3.3.12, FQBN in `tools/s3.sh` (Huge APP, QSPI PSRAM), Wi-Fi on the KGA hotspot, Mosquitto on the Mac. Tester: Hasif. Pico pins are mapped to S3 pins by `--board s3`; jumpers 1↔2 (UART) and 5↔6 (GPIO). Logs are in `parity/logs/s3/`.

| Stage | Covers | Result | Notes |
|---|---|---|---|
| `hal_test.py` (haltest sketch) | NFR13, T-05 | 23/23 | PWM duty 0.250/0.500/0.749; PULSE returns in under 50 µs, pin high 199.04 ms; 20,000 edges at 18.5 kHz, none lost |
| `4a` | P-01 to P-07, P-09, P-18, P-22, P-23 | 14/14 | Includes inference through Sitt's TFLite wrapper |
| `4b` | P-08, P-10, P-11, P-19 to P-21 | 25/25 | |
| `4c-uart` | P-16, P-17 | 8/8 | D-05 fixed: receive buffer flushed after set-up |
| `4c-mag` | P-15 (I2C), T-07 | 15/15 | Real GY-511 (LSM303DLHC) on SDA GPIO1, SCL GPIO2: magnetometer identity `48 34 33` ("H43") at 0x1E; accelerometer at 0x19 on the same bus reads about 1 g; magnetometer data at the ±8.1 gauss range (the default range overflowed near the laptop); the same identity read with and without a repeated start; a missing device (0x2A) publishes nothing and the node keeps running. Unplug-and-recover was not run |
| `5-neg` | N-01 to N-08 | 27/27 | D-01, D-02, D-03, D-04 now refused when added |
| `t02` | T-02 | 7/7 | |
| `t03` | T-03 | 6/6 | D-08 refused |
| `t04` | T-04 | 13/13 | Eight sensors keep publishing; overload is reported |
| `t06` | T-06 | 6/7 (partial) | One-wire with no sensor publishes nothing (pass). Pulse with no sensor: GPIO7 reads high on the bench even with `gpio.pull=down`, so the 25 ms timeout is reported as a valid width (D-07, unchanged for parity) |
| `t08` | T-07, T-08 | 26/26 | |
| `t09` | T-09 | 5/5 | |
| `t10` | T-10 | 7/7 | Run after a board reset: an earlier stage leaves GPIO5 driving the 5↔6 jumper |
| `mqtt_test.py` | P-25 to P-29 | 27/27 | D-09, D-10, D-12 fixed; bridging verified; the model upload is verified, saved and auto-loaded |
| `model_test.py` | T-14, T-15, T-16 | 9/9 | Valid upload saved to LittleFS and loaded; wrong CRC32, 300 KB and interrupted uploads refused with the previous model still running inference; after a reset the saved model auto-loads with no re-upload; arena 2,908 of 65,536 B used |
| `flood_test.py` | T-13 | Pass | Heartbeat kept through 2,547 messages in 60 s; no reset |
| `net_fault_test.py` | T-11, T-12 | 9/9 | Malformed JSON refused; broker loss back in 2.2 s; Wi-Fi drop back in 1.8 s with 20 samples taken offline (FR21); heap 54,200 → 53,764 B; no reset. A 5,000-byte payload exceeds the client buffer, so the library drops the connection and the node reconnects and answers; the test accepts either outcome |
| `timing_test.py` | NFR1 | Pass | Mean interval 101.28, 504.52, 1005.72 and 2019.88 ms for 100, 500, 1000 and 2000 ms targets (within 5%, ≥100 samples); 10 ms measured at 10.99 ms and 50 ms at 51.15 ms |
| `endurance_test.py` | NFR6 | 6/6 | 30 minutes with Wi-Fi and MQTT on the home network: three sensors (2, 1, 0.5 Hz) and an inference rule; no reset or panic; largest sample gap one period (E1 3,598 samples, E2 1,799, E3 901); 181 heartbeats, largest gap 10.5 s; internal heap 48,696 → 48,696 B (sampled every 5 minutes, range 48,648–48,696) |
| `TASKS` after the suite | T-01 | Pass | Stack free: SchedulerTask 12,036, MQTTTask 5,404, ControlTask 2,180, InferenceTask 7,416, ApplicationTask 3,320, BenchmarkTask 2,260 B. Internal heap 53,716 B (minimum 49,376 B); PSRAM free 2,011,040 B; no watchdog reset or panic in any log |

**Pico regression:** the Pico firmware (`project_v3`) builds with every shared-code change above and `tools/check_hal_boundary.sh` passes; the Pico hardware re-run is still to do.

**Not run on the S3:** 4c-sonic (P-12, P-13) needs a 1:2 divider on Echo and the kit has only two 50 kΩ resistors; the TSL2561 (4c-i2c) is no longer in the kit, so I2C is verified with the GY-511 (4c-mag); P-30 radar deferred as on the Pico; the Pico hardware re-run waits for a Pico on a data cable.

## 4. ESP32-S3 on µT-Kernel 3 SMP (tasks 24 to 34)

Build: `esp32s3_mtk` on the course port `sirfonzie/mtk3smp-esp32s3` at `e92874d`, ESP-IDF `v6.1-dev-2300-g17a74c925c`, `xtensa-esp-elf-gcc` 15.2.0 (`esp-15.2.0_20251204`), the port's default optimisation (`-Og`). Same HW-747 Super Mini as §3, Mosquitto 2.1.2 on the Mac, Wi-Fi on a 2.4 GHz phone hotspot. Run on 2026-10-20 by Hasif with the scripts unchanged (`sh tools/mtk.sh <stage>`); logs are in `parity/logs/mtk/`. The FreeRTOS column repeats §3 (Arduino-ESP32 3.3.12, 2026-10-13/14) so the two runtimes can be read side by side.

| Stage | Covers | FreeRTOS (Arduino) | µT-Kernel 3 SMP | Notes |
|---|---|---|---|---|
| `hal_test.py` (haltest firmware) | NFR13, T-05 | 23/23 | 27/27, 1 skipped | The four extra checks prove the interrupt-driven echo wait (task 29). PWM duty 0.250/0.500/0.749; PULSE returns in 24 µs, pin high 201.6 ms (alarm resolution 1 ms); 20,000 edges at 19.8 kHz, none lost; UART internal loopback 34/34 bytes; I2C NACK on a missing device; the 5↔6 jumper measurement is skipped because no jumper was fitted |
| `4a` | P-01 to P-07, P-09, P-18, P-22, P-23 | 14/14 | 14/14 | Inference runs in InferenceTask with MSE 0.4179, as on the Pico; one invocation takes 500–525 µs, against 265–280 µs on the Arduino build in the same session (`parity/logs/s3/wcet.log`): here each request crosses to InferenceTask and back, and this build uses the port's default `-Og` where Arduino-ESP32 builds at `-Os` (see the cycle-time notes) |
| `5-neg` | N-01 to N-08 | 27/27 | 27/27 | |
| `t03` | T-03 | 6/6 | 6/6 | |
| `t04` | T-04 | 13/13 | 13/13 | |
| `t06` | T-06 | 6/7 | 7/7 | The no-echo read now times out cleanly on GPIO7; a stuck-high echo still reports the timeout as a width (D-07, unchanged for parity, HAL test) |
| `t10` | T-10 | 7/7 | 7/7 | |
| `mqtt_test.py` | P-25 to P-29 | 27/27 | 27/27 | Includes the model upload over MQTT and inference on it |
| `model_test.py` | T-14, T-15, T-16 | 9/9 | 8/9 | Valid, corrupt, oversized and interrupted uploads all behave as on the Arduino build, with inference running throughout; arena 2,908 of 65,536 B. `persists-after-reset` misses only because this build reloads the saved model about 1 s after reset and the script reopens the port 4 s after it; the reload is captured in `31-model-persists.log` |
| `net_fault_test.py` | T-11, T-12 | 9/9 | 9/9 | Broker loss back in 2.1 s (FreeRTOS 2.2 s); after a Wi-Fi drop the node is back on the broker 5.4 s after the link returns (FreeRTOS 1.8 s; esp-mqtt retries every 5 s); 20 samples taken offline; heap 40,852 → 40,796 B; no reset |
| `flood_test.py` | T-13 | Pass | Pass | 2,544 messages at 50 per second in 60 s; heartbeat kept throughout and for 30 s after; no reset |
| `timing_test.py` | NFR1 | Pass | Pass | Table below |
| `wcet_test.py` | F6, R1 | not run then | measured | Table below |
| `endurance_test.py` | NFR6 | 6/6 | 6/6 | 30 minutes with Wi-Fi and MQTT: three sensors (2, 1 and 0.5 Hz) and an inference rule on every E1 sample; no reset or panic; largest sample gap one period (E1 3,600 samples, E2 1,800, E3 902); 181 heartbeats, largest gap 10.1 s; internal heap 39,360 → 39,380 B, sampled every 5 minutes. A first attempt was void because the laptop went to sleep with its lid closed, which stopped the script reading the board and took the broker offline; the board itself kept running |
| `TASKS` after the suite | T-01 | Pass | Pass | At the end of the endurance run every task that ran stayed on its own core (InferenceTask 3,602 runs, all on core 1; MQTTTask only on core 0). Stack free: SchedulerTask 11,292, MQTTTask 14,284, ControlTask 3,132, InferenceTask 6,460, ApplicationTask 3,420, BenchmarkTask 2,364, MonitorTask 3,308 B. No message-buffer drops. Internal heap 40,880 B, minimum 31,928 B (`32-tasks-after-suite.log`) |
| Watchdog (`STALL 30000`) | NFR6 | not tested | Pass | Feeding stops 5.3 s into the stall and timer group 0's watchdog resets the chip 10 s later (`28-watchdog.log`) |
| Core residency | Change requirement | n/a | Pass | Every task ran only on its assigned core (`28-core-residency.log`) |

**NFR1 sample timing** (Wi-Fi and MQTT active; ms):

| Target | FreeRTOS mean | FreeRTOS p95 | FreeRTOS missed | µT-Kernel mean | µT-Kernel p95 | µT-Kernel missed |
|---|---|---|---|---|---|---|
| 10 | 10.99 | 12 | 68 of 19,558 | 11.05 | 12 | 63 of 19,505 |
| 50 | 51.15 | 52 | 12 of 4,203 | 51.07 | 52 | 6 of 4,220 |
| 100 | 101.28 | 102 | 6 of 2,122 | 101.81 | 102 | 16 of 2,117 |
| 500 | 504.52 | 502 | 3 of 426 | 502.20 | 502 | 1 of 429 |
| 1,000 | 1,005.72 | 1,002 | 1 of 213 | 1,001.13 | 1,002 | 0 of 215 |
| 2,000 | 2,019.88 | 2,002 | 1 of 106 | 2,019.93 | 2,002 | 1 of 106 |

Both builds meet NFR1 at every graded rate, and both occasionally skip one sample (a gap of two periods), so the skipped samples come from the shared scheduler rather than the RTOS.

**SchedulerTask cycle time** (`wcet_test.py`, 20 s per scenario, MQTT connected; µs; the maximum is the largest observed cycle, not a proven bound):

| Scenario | FreeRTOS cycles | FreeRTOS mean | FreeRTOS p95 | FreeRTOS max | µT-Kernel cycles | µT-Kernel mean | µT-Kernel p95 | µT-Kernel max |
|---|---|---|---|---|---|---|---|---|
| Idle, sampling on, no sensors | 25 | 229 | <250 | 244 | 25 | 188 | <250 | 320 |
| 8 digital inputs at 50 ms | 1,643 | 1,447 | <5,000 | 7,006 | 748 | 1,595 | <5,000 | 3,838 |
| 1 digital input + rule at 50 ms | 794 | 692 | <2,500 | 2,336 | 410 | 1,024 | <2,500 | 1,698 |
| 1 HC-SR04 at 50 ms, no echo (25 ms timeout) | 281 | 25,522 | <50,000 | 26,098 | 277 | 26,168 | <50,000 | 26,704 |
| 2 HC-SR04 at 50 ms, no echo | 315 | 45,776 | >50,000 | 52,257 | 277 | 51,979 | >50,000 | 53,511 |
| Inference rule at 50 ms | 428 | 1,608 | <2,500 | 2,541 | 415 | 1,964 | <5,000 | 3,033 |

On both runtimes two ultrasonic reads with no echo take longer than the 50 ms window, because each read waits for its 25 ms timeout inside SchedulerTask's cycle. On µT-Kernel the CPU is free during that wait (the HAL test's spinner ran 563,596 loops), on FreeRTOS it busy-waits. The inference cycle is longer on µT-Kernel, where each invocation crosses to InferenceTask and back and the build uses the port's default `-Og` optimisation where Arduino-ESP32 uses `-Os`: one invocation takes about 500 µs against 265–280 µs on the Arduino build, measured in the same session. How much of the difference each factor causes was not measured. In the multi-sensor scenarios the FreeRTOS build woke SchedulerTask about twice as often in the same 20 s, so its mean is spread over more, shorter cycles. The FreeRTOS column comes from the second of two runs: in the first, the Arduino build stopped about 24 s after boot with a core 1 panic (`Stack canary watchpoint triggered (IDLE1)`) and restarted. That board had just been running the µT-Kernel build, whose `model` partition (0x300000–0x382000) overlaps the Arduino build's LittleFS partition (from 0x310000), so the Arduino build may have started on a file system the other build had written into; this was not investigated further.

**Memory.** Firmware 1,249,296 B on µT-Kernel against 1,339,722 B on FreeRTOS. Internal heap free with Wi-Fi and MQTT running: 40,880 B on µT-Kernel (minimum 31,928 B since boot) against 53,716 B (minimum 49,376 B) on FreeRTOS. The µT-Kernel build keeps EdgeCore's static tables and the tensor arena in PSRAM and, since task 31, the course port's kernel and shim in IRAM (MTK_REDESIGN.md §8).

**Not run on µT-Kernel (hardware pending):** 4b and t02 need the GPIO5↔GPIO6 jumper; 4c-uart, t08 and t09 need the GPIO1↔GPIO2 jumper (the light sensor uses GPIO1, so it must be removed first); 4c-mag needs the GY-511; 4c-sonic needs the HC-SR04 with its divider, as in §3. The UART, I2C and GPIO drivers are covered meanwhile by the HAL test's internal UART loopback, I2C NACK and self-driven PWM, PULSE, edge and echo checks. 4d-ir needs a hand over the light sensor and is run interactively.
