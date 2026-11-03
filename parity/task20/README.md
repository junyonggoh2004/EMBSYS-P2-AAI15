# Task 20: ESP32-S3 ML compatibility and hardware check

Status: in progress (Sitt). The built-in model now compiles, uploads, and runs
on the ESP32-S3 with the Pico reference score. Persistent model upload and
selection remain open.

## Runtime and model

- Arduino-ESP32 core: 3.3.12, as recorded in `SETUP.md`.
- Selected runtime for the built-in model: Espressif
  `espressif__esp-tflite-micro` 1.4.0, bundled with that core's ESP32-S3
  libraries. The local `versions.txt` also lists
  `espressif__esp-nn` 1.3.2. No separate Arduino Library Manager install is
  needed for the built-in model. Other approved models still need compatibility
  checks.
- The checked-in `autoencoder_int8 (2).tflite` is 16,440 bytes and matches the
  byte array in `libraries/EdgeCore/src/models/model_data.h` exactly. Its
  SHA-256 is `25be5ddf7e500e6cd58a73341df345f9422b2816fa767df8517756f36e2b2ae5`.
- The checked-in FP32 SVDD candidate is 2,272 bytes (SHA-256
  `c2755a83f2b51dbe0ae2e9ec56d09784ff6a0f3b3acf48fd1ee41ee53315c799`).
- The Pico reference registers 30 TFLite Micro operators. The S3 bridge keeps
  that resolver list and the shared `tflite_wrapper.h` interface. Header and
  bounded FlatBuffer verification run before model load; tensor element counts
  must fit the shared 16-input/16-output buffers.

The S3 bridge is `libraries/EdgeS3/src/tflite_wrapper_s3.cpp`. It includes the
TFLite Micro headers and calls the prebuilt component beneath Arduino. This is
a documented platform implementation exception; shared application code does
not call these APIs. The Pico wrapper remains unchanged.

The [SVDD probe](svdd_probe/svdd_probe.ino) uses a diagnostic byte copy of
the checked-in FP32 model. It is separate from the production firmware and
does not select that model for release.

## Checks performed

On Windows, the ESP32-S3 cross compiler from the installed Arduino-ESP32
3.3.12 package compiled `tflite_wrapper_s3.cpp` into an object with that
package's C++ flags, include paths, definitions, and `qio_qspi` SDK config.
The package link flags contain `-lespressif__esp-tflite-micro`, and the archive
exists in the package.

On 2026-10-13, Arduino IDE 2.3.10 compiled and uploaded
`esp32s3_edge/esp32s3_edge.ino` on the connected Super Mini (COM6) with
Arduino-ESP32 3.3.12. The selected profile was **ESP32S3 Dev Module**, with
USB CDC On Boot enabled, USB Mode Hardware CDC and JTAG, Upload Mode UART0 /
Hardware CDC, 4 MB flash setting, default 4 MB with SPIFFS partition scheme,
and PSRAM disabled. These are the *test settings*, not measurements of the
module's physical flash or PSRAM. The IDE reported 678,189 bytes of sketch
storage (51%); upload ended with a verified data hash and hard reset.

The serial test configured GPIO1 as a pulled-down digital input at 2 Hz and
added a batch rule containing `infer.set:0:1.0` and `infer.run`. All 40 captured
invocations returned `MSE=0.4179 mask=1000 thresh=1.1407 OK`, matching the
Pico P-23 score exactly. Invoke time was 242-462 microseconds (mean 261.9),
below the 1298 microsecond NFR-03 ceiling. The scheduler produced two samples
and inferences per second in this test; that is the observed throughput, not a
maximum-rate benchmark. A reset with Serial Monitor already open captured the
full model-load trace: one 16-element input tensor, one 16-element output
tensor, and 2,908 of 65,536 reserved arena bytes used. After the run, `TASKS`
showed 104,736 bytes free heap and 104,608 bytes minimum free heap. The
smallest reported task stack margin was 2,256 bytes. The filtered
[serial capture](s3_inference_serial.log)
retains the commands' responses, all 40 results, and the memory readings.

The SVDD probe also compiled and uploaded with the same board settings. Its
[serial capture](svdd_probe_serial.log) shows that the 2,272-byte FlatBuffer
passes schema verification and tensor allocation, but its FP32 input tensor is
`1x17` while the shared wrapper and inference manager currently support at
most 16 inputs. The wrapper returned `TFLITE_ERR_BAD_MODEL` and did not invoke
it. Its output tensor is `1x4`. Supporting this candidate would require an
agreed 17-feature mapping, normalization data, and an intentional shared
interface change (or a replacement 16-input model). Do not treat it as an
approved runtime model yet. The main `esp32s3_edge` firmware was re-uploaded
after the diagnostic test, and the IDE verified the written hash.

To repeat the test, open Serial Monitor on the board's observed port at 115200
baud with **New Line** selected, then send these lines one at a time:

```text
BEGINCFG|name=DIG|proto=gpio|gpio.mode=digital|gpio.pin=1|gpio.pull=down|mode=poll|freq_hz=2|ENDCFG
BEGINRULE|name=R_INF|source=DIG|calc=s=u8(0)|when=1|action=batch: infer.set:0:1.0 \| infer.run|ENDRULE
RUN
STOP
TASKS
```

Leave several seconds between `RUN` and `STOP`. The rule requires GPIO1 to be
unconnected or held low; GPIO1 was chosen from this board's exposed pins in
place of the Pico parity script's GP22. These commands do not upload a model.

## Remaining Task 20 checks

1. Decide whether the FP32 SVDD candidate should use an agreed 17-feature
   interface or be replaced with a 16-input model; then verify its output and
   arena use if it is selected for release.
2. Confirm physical flash capacity, then design and implement bounded
   persistent model storage for that layout. Verify upload, CRC,
   interrupted-upload recovery, reboot persistence, and
   normalization/threshold/score-mode preservation. This
   depends on Task 19 MQTT transport and a reachable broker. Do not assume the
   draft 256 KB A/B partition proposal is already deployed.

Keep Task 20 marked in progress until all Task 20 acceptance criteria and
parity evidence are complete.
