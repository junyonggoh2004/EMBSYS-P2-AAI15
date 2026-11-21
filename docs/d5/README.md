# D5: benchmarking the D4 models on the ESP32-S3

Owner: Hasif. Checklist: [README section 6](../../README.md#6-d5-task-checklist-hasif). This file holds the scope, the method, the explanation and the results. Every number was measured on the board or with the build tools on the dates given; nothing here is estimated or copied in as a result.

## 1. Scope (task 4.1)

The professor said benchmarking is not for this module and not to run the full study; comparing the configurable platform with a hard-coded build belongs in a later research paper, and the result is not about which model is best. In D4 the team chose models because they use the S3's built-in acceleration. D5 therefore measures and explains how those models run on the S3: their speed, their size and memory, how much ESP-NN's S3 assembly adds, and where the time goes. Accuracy and the cost of configurability are out of scope.

| Item | Decision (proposed 17 Nov 2026, confirmed by the team 20 Nov 2026) |
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
| sine-int8 | INT8 | Dense 1 → 16 → 16 → 1 | 1 → 1 | 2,704 | Yes; the board run of 10 Nov shows its vector calls ([BOARD_VALIDATION.md](../embedded-ml/BOARD_VALIDATION.md#acceleration-evidence)), and it is 1.36 times faster with them (section 6) |
| autoencoder | INT8 | Dense 16 → 64 → 32 → 8 → 32 → 64 → 16 | 16 → 16 | 16,440 | Yes; 2.83 times faster with it (section 6) |
| comfort-summer | Float32 | Dense 2 → 10 → 100, softmax | 2 → 100 | 5,960 | No (float) |
| people-counter | INT8 | Two convolution layers over the 31 readings (16 and 32 filters of width 4), then Dense 992 → 3, softmax | 31 → 3 | 10,736 | Yes; 3.35 times faster with it (section 6) |

Sizes and formats are from [docs/embedded-ml/README.md](../embedded-ml/README.md) and [models/classroom/README.md](../../models/classroom/README.md); the layers of the two classroom models were read from their model files. The tensor arena each model uses is printed when it loads and is recorded by the runner; the values are in section 6.

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

**Plain C build**: `NN=ansi` before any `tools/mtk.sh` command builds the same firmware with [sdkconfig.nn_ansi](../../esp32s3_mtk/sdkconfig.nn_ansi) (`CONFIG_NN_ANSI_C=y`) in its own directory, `esp32s3_mtk/build-nn-ansi` (`build-n16r8-nn-ansi` with `BOARD=n16r8`), so the normal build is untouched.

**Runner**: [tools/d5_bench.py](../../tools/d5_bench.py) sends the commands, saves the console log and `results.json` under `parity/logs/mtk/d5/`, and prints the tables. `bench` times each model, `live` collects the ML pipeline's own `invoke_us` while sensors and MQTT run (task 4.5), and `compare` puts the two builds side by side. Laptop tests: `sh tools/d5_test.sh`.

## 5. Board session (tasks 4.2 to 4.5)

One board on the same Wi-Fi network as the laptop running the broker, as for the MQTT tests (the board connects to the address in `MQTT_HOST` in its credentials file, so after the laptop changes network that line must match the laptop's new address and the firmware must be rebuilt); no sensors are needed until step 4. Use `PORT=...` and `BOARD=n16r8` as usual if needed.

| Step | Command | Gives |
|---|---|---|
| 1 | `sh tools/mtk.sh flash`, then `.venv/bin/python tools/d5_bench.py bench --port <port> --build optimized` | Every model with the S3 assembly: times, arena, vector calls, free memory |
| 2 | `NN=ansi sh tools/mtk.sh flash`, then `.venv/bin/python tools/d5_bench.py bench --port <port> --build ansi` | The same with plain C |
| 3 | `.venv/bin/python tools/d5_bench.py compare parity/logs/mtk/d5/optimized-<date>/results.json parity/logs/mtk/d5/ansi-<date>/results.json` | Task 4.2's table and speed-ups; sine-float should not change, which shows the difference comes from ESP-NN alone |
| 4 | `sh tools/mtk.sh flash` again; load the sensors and ML (**MTK: load sensors and ML**, or `ML PRESET sine-int8`, `ML RUN`, `RUN`; one sensor is enough, for example the ultrasonic alone with `ML MODEL sine-int8`, `ML RESET`, `ML BIND 0 DISTANCE distance_cm 2 400`, `ML RUN`, `RUN` as on 20 Nov); then `.venv/bin/python tools/d5_bench.py live --port <port> --samples 100` | Task 4.5: `invoke_us` with sensors and MQTT running |


## 6. Results

Tasks 4.2 to 4.4 were measured on 20 Nov 2026 on one ESP32-S3 N16R8 with no sensors, Wi-Fi and MQTT connected to the broker throughout. The firmware is `main` at 06bf79d built twice, normally and with `NN=ansi`, so only ESP-NN's kernels differ. Each model had 5 warm-up runs and then 200 timed runs on the fixed input (every input 0.5). Logs, with every run in `results.json`: [S3 assembly](../../parity/logs/mtk/d5/optimized-20261120-0224/), [plain C](../../parity/logs/mtk/d5/ansi-20261120-0225/), [provenance](../../parity/logs/mtk/d5/board-20261120.txt).

**4.2 Acceleration on against off.** `Invoke()` time in µs over 200 runs:

| Model | Format | S3 assembly, median / p95 / max | Plain C, median / p95 / max | Speed-up (medians) | Same output |
|---|---|---|---|---|---|
| sine-float | Float32 | 26 / 26 / 27 | 26 / 26 / 27 | 1.00 | Yes |
| sine-int8 | INT8 | 28 / 28 / 51 | 38 / 38 / 45 | 1.36 | Yes |
| autoencoder | INT8 | 138 / 142 / 197 | 390 / 391 / 430 | 2.83 | Yes |
| comfort-summer | Float32 | 112 / 113 / 115 | 112 / 113 / 125 | 1.00 | Yes |
| people-counter | INT8 | 1,326 / 1,379 / 2,046 | 4,436 / 4,566 / 5,089 | 3.35 | Yes |

The speed-up grows with the work in the INT8 layers: 1.36 times for sine-int8, whose first layer is too short for the vector loop, 2.83 times for the autoencoder and 3.35 times for people-counter, whose convolution layers hold most of its multiply-adds. The two Float32 models take the same time in both builds, which shows that the difference comes from ESP-NN alone, and every model gives the same output in both builds, so the S3 kernels change the time but not the result. The firmware's vector-call counter wraps only `esp_nn_fc_s16_esp32s3`, the S3 fully connected kernel that sine-int8's 16-wide layers use: it counts 2 calls per sine-int8 inference with the assembly and none in plain C. The autoencoder and people-counter run through other ESP-NN S3 kernels, so the counter shows 0 for them and their speed-up is the evidence.

**4.3 Float against INT8, and does it fit.** Arena is the tensor arena the model uses at load; times are `Invoke()` medians:

| Model | Format | File (bytes) | Arena, S3 assembly (bytes) | Arena, plain C (bytes) | `Invoke()` S3 assembly / plain C (µs) |
|---|---|---|---|---|---|
| sine-float | Float32 | 3,164 | 776 | 776 | 26 / 26 |
| sine-int8 | INT8 | 2,704 | 716 | 716 | 28 / 38 |
| autoencoder | INT8 | 16,440 | 2,908 | 2,908 | 138 / 390 |
| comfort-summer | Float32 | 5,960 | 1,432 | 1,432 | 112 / 112 |
| people-counter | INT8 | 10,736 | 6,204 | 3,916 | 1,326 / 4,436 |

sine-int8 is 15% smaller than sine-float and uses 60 B less arena, but it is not faster: 28 µs against 26 µs with the S3 assembly and 38 µs in plain C. For a network of about 290 multiply-adds the per-operator costs dominate, so INT8's gain here is size. What INT8 buys on the S3 is access to ESP-NN's assembly at all: the Float32 models cannot use it, while the larger INT8 models run 2.8 to 3.4 times faster with it. people-counter's arena is 2,288 B larger with the S3 assembly because ESP-NN's S3 convolution kernels ask TensorFlow Lite Micro for scratch space in the arena (`esp_nn_get_conv_scratch_size`), which the plain C kernels do not need; the other models have no convolution layers and use the same arena in both builds. The largest arena, 6,204 B, is under a tenth of the 64 KB arena in PSRAM. Free memory does not change with the model because the arena is a fixed buffer: with Wi-Fi and MQTT connected the board had 30,684 B of internal RAM free (lowest since boot 25,516 B, largest block 20,480 B) and 8,176,484 B of PSRAM free with the S3 assembly, and 30,720 B (lowest 25,260 B) in plain C. Everything fits; internal RAM is the limit, as section 3 found.

**4.4 Where the time goes.** Medians with the S3 assembly; the rest is the request time minus `Invoke()` for each run, which covers the round trip from SchedulerTask to InferenceTask through `infer_mbf` and back, and converting the inputs to the model's format and the outputs back:

| Model | Request (µs) | `Invoke()` (µs) | Rest (µs) | `Invoke()` share of the request |
|---|---|---|---|---|
| sine-float | 120 | 26 | 94 | 22% |
| sine-int8 | 123 | 28 | 96 | 23% |
| autoencoder | 255 | 138 | 118 | 54% |
| comfort-summer | 209 | 112 | 97 | 54% |
| people-counter | 1,535 | 1,326 | 208 | 86% |

The rest is about 95 µs for the models with one or two inputs and 118 µs for the autoencoder's 16 inputs and outputs, the same in both builds, since ESP-NN does not touch it. people-counter's rest is 208 µs with the S3 assembly and 167 µs in plain C; this run does not show where the 41 µs difference comes from. Because the rest is three quarters of a sine-int8 request, its 1.36 times faster `Invoke()` makes the whole request only 1.08 times faster (133 to 123 µs). The larger models keep most of the gain: the whole request is 1.99 times faster for the autoencoder (508 to 255 µs) and 3.00 times for people-counter (4,603 to 1,535 µs).

**4.5 In the running system.** Same board and normal firmware, with one HC-SR04 ultrasonic sensor (TRIG GPIO4, ECHO GPIO5) read twice a second, sine-int8 fed from its distance (`ML BIND 0 DISTANCE distance_cm 2 400`), the ML pipeline producing a result every 2 s and each result published over MQTT. Over 100 results the request time (`invoke_us`, the same interval as the request time in 4.2) had a median of 451 µs, a 95th percentile of 458 µs and a maximum of 641 µs, against 123 µs in `MLBENCH`. All 400 sensor readings were OK, every ML result after the first reading arrived was OK, `TASKS` showed no dropped message in any buffer and at least 2,364 B of stack free in every task, and 30,128 B of internal RAM were free at the end (lowest since boot 23,404 B) ([log](../../parity/logs/mtk/d5/live-20261120-1231/)).

The gap does not come from the sensor or the load. The code path and the timer are the same as in `MLBENCH`, so the difference comes from what runs between inferences. With sampling stopped, `ML REPLAY` of sine-int8 took a median of 349 µs when the replays followed each other, with the result printing and MQTT publishing between them, and 467 µs when they were 2 s apart, as in the live pipeline; `MLBENCH`'s 123 µs is for inferences with nothing in between. The likely cause is the S3's caches: the model and TensorFlow Lite Micro's code are read from flash, and the arena from PSRAM, through them, and other work evicts them between inferences. These runs do not separate cache misses from other effects.

To see whether 4.2's speed-up holds in this condition, the two INT8 models were replayed on both builds, 20 times back to back and 10 times 2 s apart (medians, µs):

| Model | S3 assembly, 2 s apart | Plain C, 2 s apart | Speed-up, 2 s apart | S3 assembly, back to back | Plain C, back to back |
|---|---|---|---|---|---|
| sine-int8 | 467 | 453 | 0.97 | 349 | 309 |
| people-counter | 2,065 | 5,255 | 2.55 | 1,929 | 5,160 |

With every inference starting cold, the S3 assembly still makes people-counter 2.55 times faster, saving 3.2 ms per inference, but sine-int8 gains nothing: its 10 µs saving in `MLBENCH` is lost, and it is slightly slower with the assembly in both conditions, which these runs do not explain. The 4.2 result therefore holds in the running system for the larger INT8 model, while for a model as small as sine-int8 the cost of starting cold outweighs the vector instructions ([S3 assembly log](../../parity/logs/mtk/d5/spacing-optimized-20261120.log), [plain C log](../../parity/logs/mtk/d5/spacing-ansi-20261120.log)).

**Model checks.** On the same board, `ML REPLAY 26.3 67` gives comfort-summer's desktop reference of 92, 31 zero readings give people-counter's `none` class (score 0.957), and `ML REPLAY 1.57` gives 1.003 for sine-int8 and 0.996 for sine-float ([log](../../parity/logs/mtk/d5/replay-20261120.log)).

## 7. Conclusion (task 4.6)

On the ESP32-S3, ESP-NN's S3 assembly runs the INT8 models' `Invoke()` 1.36 to 3.35 times faster than the same library's plain C, with identical outputs, and the gain grows with the work in the INT8 layers: the small sine network barely uses the vector loop, while people-counter's convolutions gain the most. Float32 models cannot use it and run at the same speed in both builds. The cost is about 36 KB of flash and 56 B of internal RAM, plus 2,288 B more arena for the convolutional model. For the smallest models the firmware's fixed cost per request, about 95 to 120 µs for the task round trip and the data conversion, is larger than the inference itself, so in the running system the speed-up shows fully only for the larger models; that cost belongs to the request path, not the model. The results support the D4 choice of INT8 models to show the S3's acceleration, with people-counter the clearest case. In the running firmware, with a sensor and MQTT active and one inference every 2 s, each inference starts from cold caches and takes several times its `MLBENCH` time (451 against 123 µs for sine-int8); in that condition the S3 assembly still makes people-counter 2.55 times faster but gives sine-int8 nothing, so the acceleration pays off for models with real work in their INT8 layers, the case people-counter represents. For D6 this means people-counter, the model for the doorway counter if D6 keeps it, fits and runs in about 2 ms per inference in the running firmware, far inside a sensor period of hundreds of milliseconds, and that internal RAM, with about 30 KB free while Wi-Fi and MQTT run, is the resource to watch rather than the ML.
