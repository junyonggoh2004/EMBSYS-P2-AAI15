# D2 SPI checkpoint results — 2026-10-16

Evidence metadata for the 2026-10-17 additions: baseline commit `efd5c9a8e3e427f4717612c2703d04dda3ec85de` plus uncommitted benchmark/fault harness and documentation changes; ESP32-S3 Super Mini / HW-747 (chip revision 0.2, 4 MB embedded flash, 2 MB PSRAM); Arduino CLI 1.5.1 and `esp32` core 3.3.12; FQBN `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=4M,PSRAM=enabled,PartitionScheme=huge_app`; 240 MHz CPU; FSPI/SPI2, SCLK GPIO11, MOSI GPIO12, MISO GPIO13, CS GPIO10, with only GPIO12↔GPIO13 looped for connected tests; `SPIClass` HAL; operator Brendan with Codex-assisted capture. ESP-IDF version and physical fixture photo remain to be recorded. The W25Q32 has not been tested.

Hardware: ESP32-S3 Super Mini / HW-747 on COM5, GPIO11 MOSI connected directly to GPIO13 MISO. The LCD and mmWave module were disconnected. SPI host/pins were `fspi`, SCLK 12, MOSI 11, MISO 13, and CS 10.

Toolchain: Arduino CLI 1.5.1, Arduino-ESP32 3.3.12, MQTT 2.5.3. Firmware used the Huge APP partition and QSPI PSRAM settings recorded in `tools/s3.sh`.

Results:

- `sh tools/check_hal_boundary.sh`: pass, including `bus/spi.c`.
- Pico 2 W shared-source build: pass; `bus/spi.c` compiled and `project_v3.elf` linked through the Pico unsupported stubs.
- Production firmware: 1,339,190 bytes flash; 177,940 bytes global RAM.
- Upload: pass; flash hash verified.
- `sh tools/s3.sh spi`: 22/22 pass.
- SPI modes 0, 1, 2, and 3: exact loopback pass at 10 MHz.
- Scheduled transfers: exact 2-byte and 128-byte loopback pass.
- Invalid host, stream mode, mode 4, reserved/duplicate pins, missing MISO, RX length 129, zero timeout, and empty transfer: rejected.
- Compatible two-device bus sharing: pass; incompatible host-pin reuse rejected; existing devices remained operational.
- 100 initialize/CLEAR cycles: pass.
- Free heap: 95,748 B before and 95,748 B after the lifecycle test.
- D1 stage `4a`: 14/14 pass.
- D1 stage `5-neg`: 27/27 pass after replacing the formerly unsupported `spi` case with `bogus`.

Raw serial evidence is in `spi.log`. Remaining D2 evidence gates are the dedicated 8–1024-byte performance/integrity harness, numerical latency/throughput results, concurrent workload, and the required W25Q32 flash proof.

## 2026-10-17 checkpoint

The D2 firmware was restored on COM5. With the TFT disconnected and GPIO12 MOSI looped to GPIO13 MISO, the SPI suite passed 22/22. The TFT remained white, so its display test is unverified. The team agreed to retain the current `SPIClass` HAL. The next gate is the dedicated 8–1024-byte benchmark. The earlier GPIO11-to-GPIO13 result above describes the previous fixture; the current wiring and Week 6 pin map use SCLK GPIO11, MOSI GPIO12, MISO GPIO13, and CS GPIO10.

The TFT is an optional display experiment. The Week 6 real-device requirement is W25Q32 SPI flash event logging.

## 2026-10-17 benchmark and restoration

- The dedicated `esp32s3_spibench` sketch called the production `hal_spi_transfer()` implementation through the `SPIClass` HAL at 10 MHz, mode 0.
- The GPIO12-to-GPIO13 loopback passed 10 warm-ups and 1,000 measured transfers at each of 8, 16, 32, 64, 128, 256, 512, and 1024 bytes: 8,000/8,000 measured transfers, zero HAL errors, zero timeouts, zero mismatched transactions, and zero mismatched bytes. The complete BEGIN-to-END capture showed no reset.
- At 1024 bytes, median latency was 2,281 µs, p95 2,285 µs, p99 2,285 µs, maximum 2,286 µs, and effective one-direction payload throughput 448,806 B/s. Transaction and byte error rates were both zero.
- Free heap was 323,732 B before and 323,764 B after the benchmark. This is a dedicated benchmark sketch, so these figures are not directly comparable to the production firmware heap measurement above.
- Raw serial output, 8,000 latency samples, complete metrics, and the per-size table are in `bench-20261017-140138/`.
- The production `esp32s3_edge` firmware was rebuilt and uploaded to COM5 afterward; flash hash verified. The restored firmware passed `parity/spi_test.py` 22/22 again.

Remaining D2 work: W25Q32 real-device read/write and event logging, injected failure handling, live sampling/inference concurrency, and final regression/documentation gates.

## 2026-10-17 module-free fault checks

- Production firmware with GPIO12-to-GPIO13 loopback: `parity/spi_fault_test.py` passed 6/6. The existing config stayed operational after 999 Hz and 40,000,001 Hz were rejected. Two-byte loopback passed at the 1 kHz and 40 MHz accepted boundaries.
- A 128-byte RX at 1 kHz with a 1 ms timeout produced `poll returned -1` and no published sample. This timeout is checked after the synchronous SPIClass transfer, so it does not interrupt a transaction already in progress. Raw evidence: `fault-connected.log`.
- With only the GPIO12-to-GPIO13 jumper removed, `parity/spi_disconnect_test.py` observed seven published 64-byte samples, each with 63 mismatched bytes. The host-side checker detected corruption, but the generic firmware SPI path published the raw bytes. This is **not** an absent-device pass; the W25Q32 layer must validate a JEDEC/status/readback response and suppress/report failures. Raw evidence: `disconnected.log`.
- The separate `esp32s3_spilocktest` sketch exercised the production HAL with TX-only traffic, so no MISO jumper was required. A 1024-byte worker transfer returned 1024 while a contending transfer returned `HAL_ERR_TIMEOUT` (-1) after 1,001 µs; device close succeeded. Raw evidence: `lock-timeout.log`.
- After the temporary lock-test sketch, the production `esp32s3_edge` firmware was rebuilt and uploaded to COM5; flash hash verified. With the GPIO12-to-GPIO13 jumper reconnected, `parity/spi_test.py` passed 22/22 again; the latest raw output is `spi.log`.

## 2026-10-17 extended loopback benchmark

Four additional complete 8–1024-byte runs passed: mode 1 at 10 MHz (`bench-20261017-145833/`), mode 2 at 10 MHz (`bench-20261017-145852/`), mode 3 at 20 MHz (`bench-20261017-145904/`), and mode 0 at 20 MHz (`bench-20261017-145915/`). Each run measured 1,000 transfers at each of eight sizes after warm-ups. Across these 32,000 additional transfers there were zero HAL errors, timeouts, and byte mismatches. Including the earlier mode-0/10-MHz baseline, five configurations and 40,000 measured transfers have passed.

For 1024-byte transfers, median latency was 2,281 µs at 10 MHz in modes 0–2 and 1,832 µs at 20 MHz in modes 0 and 3. The 20-MHz mode-0 p95 was 1,833 µs and maximum was 1,835 µs. These are loopback HAL-call times, not end-to-end sensor or W25Q32 flash-write latency. Each run retains raw serial, CSV, JSON, and a per-size report in its named folder.

The production firmware was uploaded to COM5 afterward with flash hash verification. Its connected `parity/spi_fault_test.py` check passed 6/6. The W25Q32 and concurrent application proof remain open.

## 2026-10-17 module-free regression rerun

- `tools/check_hal_boundary.sh` passed for all 17 shared C sources, including `bus/spi.c`.
- The bring-up sketch compiled with the default S3 FQBN (273,317 B flash, 21,592 B globals). The production firmware compiled with the pinned Huge APP/PSRAM FQBN (1,339,190 B flash, 177,940 B globals). The `tools/s3.sh check` wrapper itself could not find `arduino-cli` on Windows PATH, so equivalent direct Arduino CLI compile commands were used.
- On restored production firmware, D1 stage `4a` passed 14/14 (`../s3/4a-d2-20261017.log`) and stage `5-neg` passed 27/27 (`../s3/5-neg-d2-20261017.log`). `4a` exercised the built-in model inference rule, but this is not the required simultaneous flash-write/live-sampling test.
- The latest SPI suite passed 22/22 with stable heap and task-stack snapshot in `spi.log`; connected fault suite passed 6/6 in `fault-connected.log`.

## 2026-10-17 GPIO fixture diagnostic

With the user-reported GPIO5↔GPIO6 jumper, D1 stage `4b` passed 21/25 (`../s3/4b-d2-20261017.log`). Every sampled GPIO6 input stayed LOW while the rule drove GPIO5 HIGH; HIGH, TOGGLE, and edge-counter checks failed. The separate HAL sketch reported `SKIP jumper-measurements no jumper between GPIO5 and GPIO6` (`../task12/hal_test-d2-20261017.log`). The user's follow-up photos show the two blue connectors on the top two pins of that side (GPIO7 and GPIO6), leaving GPIO5 open; this explains the electrical result. The HAL run also showed 17/18 passing checks plus one unrelated `unported-tflite-reports-no-model` failure: that legacy expectation assumes no model is loaded, while the current D1 firmware includes the model. The HAL sketch source has been corrected to expect the base model and compiled, but has not yet been rerun. Do not count `4b` or the HAL suite as passed until the jumper is moved to GPIO5↔GPIO6 and both are rerun. Production firmware was restored to COM5 afterward; the upload hash verified.

After moving the GPIO7 connector to GPIO5, D1 `4b` passed **25/25** (`../s3/4b-d2-20261017-corrected.log`): GPIO6 tracked HIGH/LOW and toggle actions on GPIO5, and the edge counter saw three edges. The corrected HAL sketch then passed **23/23 with zero skips** (`../task12/hal_test-d2-20261017-corrected.log`), including 25/50/75% PWM measurements, a 199,545 µs pulse, and 20,000/20,000 counted edges. The base-model load check also passed. Production `esp32s3_edge` was restored to COM5 after the HAL sketch; upload hash verified.

With the GPIO1↔GPIO2 UART loopback fixture, D1 `4c-uart` passed **8/8** (`../s3/4c-uart-d2-20261017.log`): two distinct transmitted patterns were received exactly. No firmware change or reflashing was needed for this stage. The magnetometer stage remains unavailable without its GY-511 module.

The same UART fixture passed D1 `t08` **26/26** (`../s3/t08-d2-20261017.log`), covering 7E2 framing, partial/line reads, bounded receive overflow, and quiet-line timeout. D1 `t09` passed **5/5** (`../s3/t09-d2-20261017.log`), including 20 seconds of intact 16-byte frames at 20 Hz. These are D1 compatibility regressions, not substitutes for W25Q32 or concurrent SPI logging evidence.

Further module-free D1 regressions passed on the production firmware: `t04` **13/13** (eight GPIO sensors at 20 Hz, `../s3/t04-d2-20261017.log`), `t03` **6/6** (invalid GPIO refused, `../s3/t03-d2-20261017.log`), and `t10` **7/7** (true/false rule behaviour, `../s3/t10-d2-20261017.log`). The first `t06` attempt was 6/7 because its test configured one-wire and ultrasonic Echo on the same GPIO7; the one-wire read changed that pin to pull-up, invalidating the no-echo premise. The test fixture was corrected to use separate pins (one-wire GP22→S3 GPIO6; Echo GP18→S3 GPIO7), without changing firmware. Corrected `t06` passed **7/7** (`../s3/t06-d2-20261017-corrected.log`): ultrasonic published the no-echo frame and one-wire published no sample. Pico execution of that revised test has not been rerun.
