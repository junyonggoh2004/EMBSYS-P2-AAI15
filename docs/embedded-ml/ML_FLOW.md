# The ML flow: sensor, model, result (D4 task 2.6)

This is the path one reading takes from a sensor to a published ML result on the ESP32-S3 µT-Kernel firmware, in plain words, for the report. Commands are in [RUNNING.md](RUNNING.md); the models are described in [docs/d5 section 3](../d5/README.md#3-the-models-task-43-static-part); the code is [sensor_ml.c](../../esp32s3_mtk/components/edge_mtk/sensor_ml.c) and [inference_mtk.cpp](../../esp32s3_mtk/components/edge_mtk/inference_mtk.cpp).

## 1. The sensor is read

SchedulerTask (core 1) reads every configured sensor on its own schedule, for example the HC-SR04 twice a second. The shared decoder in [sensor_measurement.c](../../libraries/EdgeCore/src/application/sensor_measurement.c) turns the raw frame into numbers, the same decoder that produces the printed sensor JSON, and passes each reading to the ML layer. The ML layer never parses printed text back into numbers.

## 2. The reading becomes a model input

`ML BIND <slot> <sensor> <field> <min> <max>` ties a sensor field to one input slot of the model. Four fields can be bound: AHT `temperature_c` and `humidity_pct`, LIGHT `light_lux` and DISTANCE `distance_cm`. Each new reading is stored with its time and scaled to 0 to 1 with `(value - min) / (max - min)`, clamped. What the model then receives depends on the model: the two sine models take 2π times the mean of the scaled values, the autoencoder takes 16 scaled values, and comfort-summer takes the raw temperature and humidity it was trained on. `ML PRESET <model>` fills in the usual bindings in one command.

## 3. The firmware decides whether to run the model

Every 2 s by default (`ML TIMING`), SchedulerTask checks that the pipeline is running, that the loaded model has not changed, that every bound input has a reading younger than 5 s, and that the readings are no more than 2.5 s apart. If any check fails it does not run the model on old or partial data; it prints a result with `outputs:null` and the reason, `MISSING_OR_STALE`, `SAMPLE_SKEW`, `MODEL_CHANGED` or `INVALID_MAPPING`, so a fault never looks like a prediction.

## 4. The model runs in its own task

SchedulerTask does not run the model itself. It sends the inputs as one request through the `infer_mbf` message buffer to InferenceTask (core 1) and sleeps on a one-message reply buffer for at most 5 s; a mutex lets only one request through at a time, and a sequence number in the reply matches it to its request. InferenceTask writes the inputs into TensorFlow Lite Micro's input tensor, converting them to 8-bit with the model's scale and zero point for an INT8 model, calls `Invoke()`, converts the outputs back to numbers and replies. For INT8 layers `Invoke()` runs ESP-NN's S3 vector kernels, which D5 measured at 1.36 to 3.35 times faster than plain C. The model's working memory, the tensor arena, is a fixed 64 KB block in PSRAM. Keeping inference in its own task means a failed or slow model cannot corrupt the sampling state, and sampling, MQTT and the console keep their own priorities while a model runs.

## 5. The result is printed and published

SchedulerTask builds the result as one JSON line: the model, the configuration generation, the raw readings, the model inputs, the outputs, `invoke_us` (the request time from step 4) and `status:"OK"`. comfort-summer and people-counter also give the winning class and its confidence, and `ML THRESHOLD <value>` adds a yes or no decision for single-output models. The line is printed on the console and, when the setup routes results to MQTT, MQTTTask (core 0) publishes it on `pico/<node>/ml/result` at QoS 1, not retained. One result from the 20 Nov board run, with the ultrasonic bound to sine-int8: a distance of 140.287 cm scaled to 0.347, times 2π gives the input 2.183, and the model returned 0.788 with `invoke_us` 641 ([log](../../parity/logs/mtk/d5/live-20261120-1231/console.log)).

## 6. Changing the model

Five models are built into the firmware and are chosen at runtime with `ML MODEL` or `ML PRESET`, on the console or over MQTT on `pico/<node>/cmd`, or with the `ml_setup` JSON (D4 part 2). Loading a model raises the generation number and stops the pipeline until `ML RUN`, so no result mixes two models or two configurations. A new model can also be sent over MQTT and stored in flash without reflashing the firmware (D1 task 31, D4 task 2.5).

## 7. Current limits

`people-counter` uses the latest 31 consecutive readings from one bound local
ultrasonic. The live pipeline waits until the window is full, then predicts
again on each new reading. An invalid echo or a timing gap clears the
window. `ML REPLAY` still accepts one complete, preprocessed window.
The model reports the direction of that window. Board A's two-sensor
`DOORWAY` rule keeps the live entry and exit totals.
