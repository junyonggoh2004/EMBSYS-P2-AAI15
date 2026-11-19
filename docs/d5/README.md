# D5: benchmarking the D4 models on the ESP32-S3

Owner: Hasif. Checklist: [README section 6](../../README.md#6-d5-task-checklist-hasif). This file holds the scope, the method, the explanation and the results. Numbers marked *pending* have not been measured yet; nothing here is estimated or copied in as a result.

## 1. Scope (task 4.1)

The professor said benchmarking is not for this module and not to run the full study; comparing the configurable platform with a hard-coded build belongs in a later research paper, and the result is not about which model is best. In D4 the team chose models because they use the S3's built-in acceleration. D5 therefore measures and explains how those models run on the S3: their speed, their size and memory, how much ESP-NN's S3 assembly adds, and where the time goes. Accuracy and the cost of configurability are out of scope.

| Item | Decision (proposed 17 Nov 2026, for the team to confirm) |
|---|---|
| Models | The five bundled models on `main`: sine-float, sine-int8, the autoencoder, comfort-summer and people-counter |
| Numbers | Inference time (request and `Invoke()` alone), model bytes, tensor arena bytes, free internal RAM and PSRAM, speed-up |
| Run rule | Five warm-up inferences, then at least 100 timed inferences per case (the runner uses 200), on one fixed input; report the count, median, 95th percentile, minimum, maximum and mean |
| Fair comparison | Same board, same firmware source and settings; only the item compared changes (ESP-NN's S3 assembly against its plain C, or the model) |
| Not measured | Accuracy, configurable against hard-coded, energy |

## 2. How the S3 speeds up INT8 models (task 4.6, explanation)

The ESP32-S3's two Xtensa LX7 cores have extra vector instructions on 128-bit registers (`q0` to `q7`). One `ee.vld.128` loads 16 signed 8-bit values, and one `ee.vmulas.s8.accx` multiplies 16 pairs of them and adds the products into an accumulator. A plain C loop does one multiply-add per step, so a layer of 8-bit weights can run several times faster in this form. Espressif's ESP-NN library holds these kernels as S3 assembly; for example its fully connected kernel (`src/fully_connected/esp_nn_fc_s8_mac16_esp32s3.S`) runs this load and multiply-accumulate loop over the weights.

TensorFlow Lite Micro, as packaged by Espressif (`esp-tflite-micro` 1.4.0 with ESP-NN 1.4.1), calls ESP-NN for its INT8 fully connected, convolution, depthwise convolution, pooling, add, multiply and softmax operators. Which ESP-NN version runs is a build setting: `CONFIG_NN_OPTIMIZED` (the default) picks the S3 assembly, `CONFIG_NN_ANSI_C` the plain C version of the same functions, which ESP-NN keeps for checking. ESP-NN has only 8-bit kernels, so a Float32 model runs on TensorFlow Lite Micro's plain reference code whatever the setting: it cannot use the S3's vector instructions. This is why the D4 models that are meant to show the acceleration are INT8. Espressif's own figure for one fully connected layer (271 inputs, 3 outputs) on the S3 is 8,439 timer ticks in plain C against 1,543 with the assembly, 5.5 times faster ([ESP-NN README](https://github.com/espressif/esp-nn)); D5 measures what this gives for whole models on our firmware.

Two things limit the gain for our models. They are small: sine-int8 is three fully connected layers (1 → 16 → 16 → 1, about 290 multiply-adds), and the vector loop only runs for rows of at least 8 values, so its first layer cannot use it; the autoencoder (16 → 64 → 32 → 8 → 32 → 64 → 16, about 6,700 multiply-adds) gives the kernels more work, and people-counter, a small convolutional network over 31 distance readings (about 68,000 multiply-adds, most of them in its second convolution layer), the most. It is also the only model with convolution layers, so it shows ESP-NN's convolution kernels as well as the fully connected one. And every inference has fixed costs that the assembly does not touch: TensorFlow Lite Micro's per-operator dispatch, converting inputs to 8-bit and outputs back, and on this firmware the request from SchedulerTask to InferenceTask and the reply through a µT-Kernel message buffer. Task 4.4 measures that second part separately. The firmware is built with the course port's `-Og` optimisation, which slows the C code (the plain C kernels and the overheads) more than the hand-written assembly; both builds use it, so the comparison is fair, but the speed-up applies to this build.

## 3. The models (task 4.3, static part)

| Model | Format | Layers | Inputs → outputs | File (bytes) | Uses ESP-NN's S3 assembly |
|---|---|---|---|---|---|
| sine-float | Float32 | Dense 1 → 16 → 16 → 1 | 1 → 1 | 3,164 | No (float) |
| sine-int8 | INT8 | Dense 1 → 16 → 16 → 1 | 1 → 1 | 2,704 | Yes; the board run of 10 Nov shows its vector calls ([BOARD_VALIDATION.md](../embedded-ml/BOARD_VALIDATION.md#acceleration-evidence)) |
| autoencoder | INT8 | Dense 16 → 64 → 32 → 8 → 32 → 64 → 16 | 16 → 16 | 16,440 | Expected (INT8); shown by 4.2 |
| comfort-summer | Float32 | Dense 2 → 10 → 100, softmax | 2 → 100 | 5,960 | No (float) |
| people-counter | INT8 | Two convolution layers over the 31 readings (16 and 32 filters of width 4), then Dense 992 → 3, softmax | 31 → 3 | 10,736 | Expected (INT8); shown by 4.2 |

Sizes and formats are from [docs/embedded-ml/README.md](../embedded-ml/README.md) and [models/classroom/README.md](../../models/classroom/README.md); the layers of the two classroom models were read from their model files. The tensor arena each model uses is printed when it loads and is recorded by the runner (section 5).

**Build sizes**, measured on 19 Nov 2026 with `idf.py size` and `idf.py size-components` on both builds of the same source, `main` with the five models and the D5 changes (raw tables in [parity/logs/mtk/d5/static-20261119](../../parity/logs/mtk/d5/static-20261119/)):

| | S3 assembly (normal build) | Plain C (`NN=ansi`) | Difference |
|---|---|---|---|
| ESP-NN library, flash | 42,093 B | 6,386 B | +35,707 B |
| ESP-NN library, internal RAM | 56 B | 0 B | +56 B |
| Whole image | 1,324,657 B | 1,288,657 B | +36,000 B (2.8%) |
| Internal RAM used (DIRAM) | 333,751 B (97.66%) | 333,695 B | +56 B |
| App partition left | 57% | 58% | |

The S3 assembly costs about 36 KB of flash, 1.2% of the 3 MB app partition, and 56 bytes of internal RAM; the 64 KB tensor arena is in PSRAM in both builds. The D5 code itself (`MLBENCH` and the `Invoke()` timer) adds 2,400 B of flash and 32 B of internal RAM to `main`. Internal RAM is the tight resource (97.7% used before Wi-Fi starts), so the free memory with Wi-Fi and MQTT running is measured on the board (section 5).

## 4. Method and tools

**MLBENCH** (console or `pico/<node>/cmd`, after `STOP` and `ML MODEL <id>`): `MLBENCH <runs> [RAW]` runs the loaded model five times as a warm-up and then `<runs>` times (10 to 1000) on a fixed input (every input 0.5), in SchedulerTask, the task that runs the ML pipeline. For every run it records the request time, `inference_mgr_set_inputs()` and `inference_mgr_run()` as SchedulerTask sees them (the same interval as `invoke_us` in the ML results, including the round trip to InferenceTask), and the time of `Invoke()` alone inside InferenceTask, and their difference. Nothing is printed while timing. It then prints the median, 95th percentile, minimum, maximum and mean of each, one JSON line, and the free internal RAM and PSRAM with Wi-Fi and MQTT still running. `RAW` also prints every run. Timing uses the firmware's microsecond clock (`hal_time_us()`). Code: [ml_bench.c](../../esp32s3_mtk/components/edge_mtk/ml_bench.c); the `Invoke()` timer is in [inference_mtk.cpp](../../esp32s3_mtk/components/edge_mtk/inference_mtk.cpp).

**Plain C build**: `NN=ansi` before any `tools/mtk.sh` command builds the same firmware with [sdkconfig.nn_ansi](../../esp32s3_mtk/sdkconfig.nn_ansi) (`CONFIG_NN_ANSI_C=y`) in its own directory, `esp32s3_mtk/build-nn-ansi`, so the normal build is untouched.

**Runner**: [tools/d5_bench.py](../../tools/d5_bench.py) sends the commands, saves the console log and `results.json` under `parity/logs/mtk/d5/`, and prints the tables. `bench` times each model, `live` collects the ML pipeline's own `invoke_us` while sensors and MQTT run (task 4.5), and `compare` puts the two builds side by side. Laptop tests: `sh tools/d5_test.sh`.

## 5. Board session (tasks 4.2 to 4.5)

One board on the hotspot with the broker running, as for the MQTT tests; no sensors are needed until step 4. Use `PORT=...` and `BOARD=n16r8` as usual if needed.

| Step | Command | Gives |
|---|---|---|
| 1 | `sh tools/mtk.sh flash`, then `.venv/bin/python tools/d5_bench.py bench --port <port> --build optimized` | Every model with the S3 assembly: times, arena, vector calls, free memory |
| 2 | `NN=ansi sh tools/mtk.sh flash`, then `.venv/bin/python tools/d5_bench.py bench --port <port> --build ansi` | The same with plain C |
| 3 | `.venv/bin/python tools/d5_bench.py compare parity/logs/mtk/d5/optimized-<date>/results.json parity/logs/mtk/d5/ansi-<date>/results.json` | Task 4.2's table and speed-ups; sine-float should not change, which shows the difference comes from ESP-NN alone |
| 4 | `sh tools/mtk.sh flash` again; load the sensors and ML (**MTK: load sensors and ML**, or `ML PRESET sine-int8`, `ML RUN`, `RUN`); then `.venv/bin/python tools/d5_bench.py live --port <port> --samples 100` | Task 4.5: `invoke_us` with sensors and MQTT running |


## 6. Results

| Task | Result |
|---|---|
| 4.2 Acceleration on against off | *pending board run* |
| 4.3 Float against INT8 and memory | Static part in section 3; arena, timing and free memory *pending board run* |
| 4.4 Where the time goes | *pending board run* |
| 4.5 In the running system | *pending board run* |

## 7. Conclusion (task 4.6)

*Pending the results.*
