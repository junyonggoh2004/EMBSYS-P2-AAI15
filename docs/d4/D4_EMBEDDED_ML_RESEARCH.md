# D4 Deep Dive - Embedded ML Efficiency on ESP32-S3

Updated: 2026-09-26

## 1. Deliverable interpretation

D4 requires a baseline model and an optimized model that are both deployable on the ESP32-S3. Accuracy, model size, RAM use, and latency must be measured on the target hardware. Optimization should include reduced complexity, an integer-quantized path where supported, and compact features or a shorter input window. The final selection must be justified using the measured accuracy/resource trade-off rather than PC estimates.

The fair interpretation is that the baseline and optimized models perform the **same task on the same held-out event set**. Comparing an anomaly detector with a multiclass classifier may be useful research, but it is not a controlled before/after quantization result.

## 2. Current repository baseline

Models present:

| Model | Exact size | Apparent task | Current firmware support |
| --- | ---: | --- | --- |
| `autoencoder_int8 (2).tflite` | 16,440 bytes | Reconstruction anomaly score | Integrated as baked-in model; MSE scoring supported |
| `svdd_encoder_fp32.tflite` | 2,272 bytes | One-class latent-distance anomaly score | Uploadable; distance scoring supported |
| `forecast_model_int8.tflite` | 4,104 bytes | Temporal prediction/error | File exists; no dedicated scoring path or code reference found |
| Rule engine | N/A | Deterministic event logic | Integrated and required as the D6 comparison baseline |

Current inference implementation:

- `TFLITE_ARENA_SIZE` is fixed at 64 KiB.
- Input capacity is fixed at 16 floats, arranged conceptually as four slots of four features.
- FP32 and INT8 tensor I/O are supported, but the public path always uses float arrays and performs per-element quantize/dequantize conversion.
- Thirty operators are registered for every model, including convolution, pooling, dense, shape, quantization, and LSTM operators.
- `arena_used_bytes()` is printed at load time.
- Invocation time is recorded, but only the most recent value is retained.
- Score modes are limited to autoencoder MSE and SVDD distance.
- Class labels, argmax, confidence, rejection thresholds, and forecast-error scoring are absent.
- Model storage is hard-coded to the last 256 KiB of a presumed 4 MiB Pico flash device.
- Parameters carry means, standard deviations, mask thresholds, and one score-mode byte, but no model manifest or feature-schema version.

Consequences:

- The existing anomaly models can form a portability baseline, but they do not directly satisfy people-flow classification.
- The forecast model should not be described as deployed until its input semantics, output semantics, and scoring code are established.
- The Pico flash implementation must be replaced with an ESP-IDF partition design.
- Registering only the operators required by the active model is a straightforward flash/code-size optimization.

## 3. Define the ML task before choosing a model

The proposed end-to-end use case requires ground-truth classes covering:

- entry
- exit
- incomplete crossing or turn-around
- false/object event

An anomaly detector answers “is this unlike normal data?” It does not inherently answer “entry or exit?”. A forecast model answers “what should the next signal look like?” and needs a separate decision rule. A multiclass classifier directly matches the required event labels.

Recommended D4 task:

> Given one completed, quality-checked event window, classify it as entry, exit, incomplete/turn-around, or false/object, with an optional reject/unknown outcome when confidence or input validity is insufficient.

The rule engine should run on the same events as a non-ML baseline.

## 4. Sensor observability limitation

Direction cannot be recovered reliably from a single static scalar measurement. Entry versus exit needs a time-ordered spatial cue, such as:

- two sensors at different positions and their trigger order
- a TCRT5000 crossing followed by or preceding an ultrasonic-distance change
- a distance trajectory whose geometry produces direction-specific shapes

The TSL2561 is useful as ambient context but is too slow and scene-dependent to be the primary direction signal. The HC-SR04 is not ready until its 5 V Echo is level-shifted. Therefore, final four-class dataset collection may be blocked until the sensor geometry and safe HC-SR04 connection are established.

Before training, draw the entrance, sensor positions, sensing directions, expected trigger order for both travel directions, and event start/end rules. If the physical arrangement cannot create direction-specific evidence, no model optimization can recover the missing information.

## 5. Candidate model ladder

| Candidate | Purpose | ESP32-S3 cost | Use in this project |
| --- | --- | --- | --- |
| Rules/state machine | Deterministic sequence baseline | Minimal | Required comparison baseline |
| Logistic/linear softmax classifier | Minimum supervised ML baseline | Very low | Establish whether features are already separable |
| Small dense MLP | Nonlinear feature classifier | Low | Recommended primary candidate |
| Small 1D CNN/TCN | Learns short temporal patterns | Moderate | Use only if compact features lose necessary timing shape |
| Autoencoder | Unsupervised anomaly detection | Low-moderate | Existing deployable baseline; supplementary alarm |
| SVDD encoder | One-class anomaly detection | Low | Existing compact research model; supplementary alarm |
| Forecasting model | Prediction-error detection | Low-moderate | Keep only if temporal prediction adds measured value |
| LSTM/GRU | Stateful sequence model | Higher complexity | Not first choice; justify against CNN/MLP |

Recommended experiment ladder:

1. Rules/state machine.
2. FP32 linear classifier.
3. FP32 small MLP.
4. Fully INT8 version of the same selected MLP architecture.
5. Small fully INT8 1D CNN/TCN only if the MLP fails on ambiguous temporal events.
6. Existing autoencoder/SVDD as optional anomaly detectors, reported separately from multiclass accuracy.

## 6. Proposed compact feature schema

The existing 16-input limit can be used for one event-level vector:

| Index | Feature | Rationale |
| ---: | --- | --- |
| 0 | event duration | Distinguishes crossing from brief noise/dwell |
| 1 | first-trigger identity/order | Primary direction cue |
| 2 | delay between sensor triggers | Direction and speed cue |
| 3 | number of direction reversals | Turn-around cue |
| 4 | minimum distance | Closest approach |
| 5 | distance at event start | Trajectory context |
| 6 | distance at event end | Trajectory context |
| 7 | signed distance slope | Direction cue if geometry supports it |
| 8 | peak absolute distance slope | Speed/dynamics |
| 9 | time below distance threshold | Dwell/occupancy cue |
| 10 | TCRT digital crossing count | Passage/noise cue |
| 11 | TCRT active duration | Object width/speed proxy |
| 12 | TCRT analog peak or contrast | Reflectance/object cue |
| 13 | ambient lux | Context/quality feature |
| 14 | valid-sample ratio | Data-quality feature |
| 15 | maximum sensor age/staleness | Prevents stale input appearing normal |

This is a proposed schema, not a final one. Freeze the exact units, normalization, missing-value behavior, feature order, and version before collecting the main dataset. Firmware and training code must share one machine-readable schema.

If raw windows are required, compare short lengths such as 16, 32, and 64 time steps. Do not use a longer window without showing an accuracy benefit that justifies its RAM and latency cost.

## 7. Dataset and ground truth protocol

Record one row per event plus the underlying synchronized sensor trace.

For every trial capture:

- event ID and subject/object ID
- ground-truth class
- direction and expected occupancy delta
- timestamped raw sensor readings
- validity, saturation, timeout, and freshness flags
- runtime sensor configuration
- sensor placement and environment ID
- firmware version

Include variation in:

- people and walking speeds
- pauses and turn-arounds
- carried objects and non-person objects
- simultaneous or closely spaced events if in scope
- ambient light
- approach angle and distance
- missing, stale, saturated, and timed-out readings

Split by subject, session, or trial group. Do not randomly split adjacent samples from the same event across training and test sets because that leaks nearly identical data into both sets.

The final locked test set must be untouched by feature/model selection. Report the number of events per class, not only the number of sensor rows.

## 8. Controlled baseline-to-optimized comparison

Recommended primary comparison:

| Property | Baseline | Optimized |
| --- | --- | --- |
| Task | Four-class event classification | Same |
| Architecture | Small FP32 MLP | Same architecture, fully INT8 |
| Input | Initial feature set/window | Reduced feature set or shorter window if validated |
| Test events | Locked held-out set | Same set |
| Firmware | Same ESP-IDF/component build | Same build except model/required ops |
| CPU/memory placement | Fixed | Fixed |

This isolates the effects of quantization and input reduction. If the optimized model also changes architecture, add an ablation table:

1. FP32 original architecture and original input.
2. INT8 original architecture and original input.
3. INT8 reduced architecture and original input.
4. INT8 reduced architecture and compact input.

Without these steps, an observed difference cannot be attributed clearly to quantization, architecture reduction, or feature reduction.

## 9. Quantization strategy

Use full-integer INT8 quantization of both weights and activations. TensorFlow requires a representative dataset to calibrate activation ranges. Enforce INT8 input and output tensors rather than leaving float boundary tensors if the goal is a fully integer firmware path.

Representative samples must cover:

- every class
- realistic feature extremes
- ordinary and ambiguous events
- valid missing-data encodings
- different environments and subjects

After conversion, verify:

- input/output tensor types are INT8
- every deployed operator has an ESP-TFLite-Micro implementation
- quantization scales and zero points are recorded
- model accuracy is evaluated after conversion
- ESP32-S3 output agrees with desktop TFLite output within a documented tolerance

Primary reference: <https://www.tensorflow.org/model_optimization/guide/quantization/post_training>

If post-training quantization causes unacceptable loss, try quantization-aware training. Pruning is lower priority: zeros do not automatically reduce latency or file size unless the export format and deployed kernels exploit sparsity.

## 10. ESP32-S3 inference stack

Replace Pico-specific `pico-tflmicro` with Espressif's `esp-tflite-micro` component and enable ESP-NN optimized kernels. ESP-NN includes ESP32-S3 assembly implementations that use the chip's vector instructions.

Primary references:

- <https://github.com/espressif/esp-tflite-micro>
- <https://github.com/espressif/esp-nn>

Important implementation work:

- pin exact ESP-IDF, `esp-tflite-micro`, and ESP-NN versions
- use `MicroMutableOpResolver<N>` containing only required operators
- add classifier output handling: argmax, confidence, labels, and rejection threshold
- add a forecast-error scorer only if the forecast model remains in scope
- measure ESP-NN enabled and disabled once to prove its effect on the selected model
- keep the active tensor arena in internal SRAM when it fits
- evaluate PSRAM separately rather than mixing memory placement in one comparison
- remove avoidable float conversion from the final integer path

Espressif's published speedups for large vision models prove that ESP-NN can matter, but they are not performance predictions for this project's small dense model. Only on-device measurements of the selected model count as D4 evidence.

## 11. Memory and arena optimization

The current 64 KiB static arena is a safe development ceiling, not evidence of actual consumption.

For each candidate record:

- `.tflite` file bytes
- `arena_used_bytes()` after allocation
- heap before model load
- heap after model load
- minimum free heap during the run
- largest free block
- internal versus external memory placement
- stack high-water mark of the inference task

Recommended arena procedure:

1. Load the model with a generous arena.
2. Record `arena_used_bytes()` across representative inputs.
3. Set the production arena to measured use plus a documented margin.
4. Re-run load, inference, stress, and model-switch tests.

Use ESP-IDF heap APIs to distinguish total free memory from the largest allocatable block and capability-specific memory.

Primary reference: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/mem_alloc.html>

## 12. Configurable model storage

Replace the hard-coded Pico flash address with a custom ESP-IDF data partition. Prefer two slots:

```text
active slot A
candidate slot B -> receive -> hash -> validate -> allocate -> self-test -> activate
```

A model manifest should include:

- magic and manifest version
- model ID/version and byte length
- CRC32 or SHA-256
- TFLite schema version
- input/output shapes and tensor types
- feature-schema ID/version
- normalization method and parameters
- quantization scale/zero point
- required operator list
- required arena bytes
- score mode or class labels
- confidence/anomaly thresholds

Do not mark a candidate active merely because all bytes arrived. Validate its hash, schema, tensors, operators, arena allocation, and a small known-input self-test. If any step fails, keep the previous active model.

Primary reference: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/partition.html>

## 13. B5 benchmark protocol

Fix these conditions before comparing models:

- physical ESP32-S3 and memory variant
- CPU frequency and power-management configuration
- ESP-IDF and component versions
- compiler optimization level
- tensor-arena memory placement
- inference task core/priority
- sensor and communications workload
- held-out event set

For each model:

1. Boot or reload into a clean known state.
2. Record model and runtime metadata.
3. Run at least 10 warm-up invocations.
4. Run at least 1,000 timed invocations or replay the locked test set enough times for stable percentiles.
5. Time `Invoke()` with `esp_timer_get_time()`.
6. Record median, p95, p99, and maximum latency.
7. Record all memory metrics.
8. Evaluate the locked test events and save every prediction.
9. Repeat with SPI TFT traffic and normal sensor acquisition active.

Classification metrics:

- confusion matrix
- per-class precision and recall
- per-class F1
- macro-F1
- overall accuracy
- reject/unknown rate
- false entry and false exit events
- final occupancy-count error over each scenario

Reliability metrics:

- model-load failures
- allocation failures
- invocation failures
- stale/invalid-input decisions
- missed sensor samples
- resets/watchdog events

Do not report accuracy alone on an imbalanced dataset.

## 14. Optimization order

Apply optimizations in this order because it preserves interpretability of the results:

1. Prove the task and ground truth are valid.
2. Establish rule and linear-classifier baselines.
3. Select the smallest FP32 MLP that meets the task.
4. Apply full INT8 quantization.
5. Remove unused operators and right-size the arena.
6. Enable and measure ESP-NN.
7. Reduce features or window length using ablation results.
8. Reduce layer width/depth if accuracy permits.
9. Consider quantization-aware training if needed.
10. Consider a 1D CNN/TCN only if the feature MLP is insufficient.

Knowledge distillation and pruning are optional experiments, not first-line work for models already measured in kilobytes.

## 15. Proposed selection rule

Numerical thresholds require supervisor approval. A defensible selection procedure is:

1. Reject any model that cannot load reliably or violates the real-time inference deadline.
2. Reject any model whose per-class recall makes entry or exit counting unsafe for the agreed use case.
3. Among the remaining models, select the smallest/fastest model within the agreed macro-F1 loss from the best model.
4. Use occupancy-count error and false-entry/false-exit behavior as tie-breakers.
5. Document why the selected model is preferable to both the rule baseline and the most accurate larger model.

Proposed starting discussion values, not approved requirements:

- no model-load or invoke failures during the benchmark
- optimized macro-F1 within 2 percentage points of the FP32 baseline
- no class loses more than 5 percentage points of recall without explicit justification
- inference p95 below the event update interval
- model plus arena fits internal memory with the agreed safety margin

## 16. Implementation sequence

1. Freeze event definitions, sensor geometry, and data protocol with D6.
2. Port the unchanged current autoencoder to ESP32-S3 to establish toolchain and memory baselines.
3. Implement general model metadata and classifier output handling.
4. Collect a pilot dataset and verify that direction is observable.
5. Train rule, linear, and small FP32 MLP baselines.
6. Export a fully INT8 version of the selected architecture.
7. Implement the partition/manifest/rollback path.
8. Run controlled B5 comparison and feature/window ablations.
9. Select the final model using approved criteria.
10. Repeat representative runs while D2 SPI traffic is active.

## 17. Evidence checklist

- [ ] ML task and class definitions frozen
- [ ] Sensor geometry proves direction is observable
- [ ] Dataset protocol and leakage-safe split documented
- [ ] Rule baseline evaluated on the same events
- [ ] Baseline and optimized models both deploy on ESP32-S3
- [ ] Controlled FP32-versus-INT8 comparison completed
- [ ] Exact model, arena, heap, and stack use recorded
- [ ] Median/p95/p99/max latency recorded on device
- [ ] Confusion matrix and macro-F1 reported
- [ ] Occupancy-count error reported
- [ ] Model manifest and failed-update rollback demonstrated
- [ ] ESP-NN effect measured for the chosen model
- [ ] Concurrent SPI/sensor/inference run measured
- [ ] Final selection rationale stated

## 18. Decisions still required

1. Exact ESP32-S3 module, flash, PSRAM, and CPU configuration.
2. Final sensor placement and whether direction is observable.
3. Approved event labels and reject/unknown behavior.
4. Ownership and deadline for dataset collection.
5. Required inference deadline and accuracy/resource thresholds.
6. Runtime model switching with or without reboot.
7. Whether anomaly detection complements the classifier or is removed from the final path.

