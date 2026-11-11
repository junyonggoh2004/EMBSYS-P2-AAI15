# dev-jy-ml — configurable embedded ML deliverables

Plan dated 3 November 2026. Base: merged `main` commit `b496a8d87135a3763403b97d3d34271b29b30442`. Target: ESP32-S3 Super Mini, ESP-IDF with µT-Kernel 3 SMP, using the existing EdgeCore framework. **Status: a first local implementation now builds and passes host tests. See [RUNNING.md](RUNNING.md) and [VALIDATION.md](VALIDATION.md). The full deliverables below remain the target scope; bundled model selection is implemented, arbitrary model-file installation and runtime manifest persistence are not. [Board inference and ESP-NN execution are verified for the local model-swap demo](BOARD_VALIDATION.md).**

## Current ML model: specifications and meaning

The default **sine-float** model is the official pretrained TensorFlow Lite Micro Hello World network. Its task is to approximate **y = sin(x)**. It was trained on synthetic numerical sine-wave pairs, with x in approximately [0, 2π], not on temperature, humidity, light or ultrasonic measurements.

| Specification | sine-float | sine-int8 |
|---|---|---|
| Architecture | Dense 1 → 16 → 16 → 1 with ReLU hidden layers and a linear output | Same 321-parameter network, quantized |
| File size | 3,164 bytes | 2,704 bytes |
| Input/output | One scalar in, one scalar out, shape [1,1] | Same shapes |
| Tensor precision | Float32 | Int8, quantized/dequantized using actual tensor parameters |
| Prediction | Approximation of sin(input) | Approximation of sin(input) |

In the full three-sensor preset, the four physical features are temperature (-10 to 50 °C), humidity (0–100%), light (0–1000 lux) and distance (2–400 cm). Each is normalized and clamped to [0,1]. The actual scalar passed to the model is:

```text
u[i] = clamp((measurement[i] - minimum[i]) / (maximum[i] - minimum[i]), 0, 1)
x = 2π × mean(u[0], u[1], u[2], u[3])
prediction ≈ sin(x)
```

All three sensors, including ultrasonic distance, belong to the default setup. Use **MTK: load sensors and ML**. The separate AHT/light-only task is an optional fault-isolation fallback, not the default.

For desktop input 1.829175, the bundled float model returned approximately 0.95309836 and the int8 model 0.95346002. These are actual desktop reference results, not ESP32 measurements.

The output is a unitless demonstration score with a target range of approximately −1 to +1. The INT8 model dequantizes its output with `y = (q - 5) × 0.0082909567`, giving a representable range of about −1.103 to +1.011. The sensor ranges control input scaling. As the normalized mean increases, the sine curve can rise or fall. A cutoff such as `0.5` tests the model score. See [the threshold run guide](RUNNING.md#set-a-scalar-output-threshold).

The existing `autoencoder` is a 16,440-byte INT8 reconstruction model with dense layers `16 → 64 → 32 → 8 → 32 → 64 → 16`. Its original training data are unverified. The demo repeats the four normalized features to fill its 16 inputs and returns the 16 reconstructed values. The different tensor shape lets the demo exercise a broader model swap.

The sine pair was chosen for its small pretrained assets and predictable reference outputs. The float model provided a reference for the same regression task in INT8, whose fully connected layers exercised the ESP32-S3 vector instructions through ESP-NN. [Board validation](BOARD_VALIDATION.md)

Pinned upstream [training source](https://github.com/tensorflow/tflite-micro/blob/964e43d7ee3e82fe6abb6075b7f50506eafce79b/tensorflow/lite/micro/examples/hello_world/train.py), [model assets](https://github.com/tensorflow/tflite-micro/tree/964e43d7ee3e82fe6abb6075b7f50506eafce79b/tensorflow/lite/micro/examples/hello_world/models), and local [artifact metadata](../../models/hello_world/manifest.json) document provenance, checksums, tensor metadata and license.

## Goal and scope

Let a user select a supported sensor/driver, configure its bus, map its decoded measurements into model features, and select a compatible pretrained model. The firmware must produce actual inference outputs after supported sensor or model substitutions without editing application code or rebuilding the firmware.

The first two deliverables are **configurable sensor-to-ML inputs** and **replaceable ML models**. Small-model research is their first implementation step. MQTT integration follows their offline acceptance tests. Accuracy and prediction usefulness are not acceptance criteria for this initial integration; correctness of execution, configuration validation and resource fit are.

This supplements [D4_EMBEDDED_ML_RESEARCH.md](../../D4_EMBEDDED_ML_RESEARCH.md). That earlier research describes a broader baseline/optimized-model study with accuracy, latency and memory measurements. This functional milestone does not establish that those course requirements have been waived or completed. Retain basic memory/latency evidence now; handle task-specific training and optimization as a separate milestone if required.

LCD remains disabled and non-functional (white/dim screen). Keep the established [wiring](../../esp32s3_mtk/WIRING.md); no display dependency is introduced.

## Baseline at branch start and planned changes

| Area | Existing main implementation | Work needed for this milestone |
|---|---|---|
| Sensor configuration | BEGINCFG, bus registry, periodic scheduler; AHT/TSL2561 profiles and GPIO pulse capture | A validated measurement-to-feature binding independent of bus and driver |
| Physical measurements | AHT, TSL2561 and distance console JSON decoders | Reusable typed numeric measurements; do not parse the console JSON back into ML |
| Inference engine | TFLite Micro on a dedicated µT-Kernel InferenceTask | Generic output mode and stricter model/input compatibility checks |
| Tensor boundary | Float32 and int8 conversion; maximum 16 input and 16 output elements | Explicit supported tensor contract, finite/scale checks and no silent padding/truncation |
| Features/scoring | Four groups of four features; baked-in normalization; MSE/distance modes | Per-model feature order, preprocessing and optional scoring rather than anomaly-only assumptions |
| Model storage | Built-in model, CRC-checked two-slot ESP-IDF partition storage; MQTT upload path | Manifest binding, offline installation path and validated activation/recovery |
| Networking | Existing MQTT task and model transfer | Preserve existing interfaces; integrate ML result messages after offline operation works |

The old research's Pico-only storage warning is historical: current main already has ESP-IDF model partitions. Reuse that implementation. Sensor JSON reporting alone does not prove that sensor data reaches the model.

## Initial pretrained-model research decision

**Start with the upstream TensorFlow Lite Micro Hello World pretrained sine model.** The upstream example supplies float and int8 model assets and tests; its training source describes a scalar-input dense network (16, 16, 1 neurons). It is an execution demonstration, not a trained environmental detector.

| Candidate | Role | Decision / evidence needed |
|---|---|---|
| Upstream `hello_world_float.tflite` | First small pretrained model; one scalar in, one scalar out | Preferred first bring-up; inspect the actual pinned artifact before claiming compatibility |
| Upstream `hello_world_int8.tflite` | Replacement model with a different tensor dtype | Required second artifact for float/int8 replacement demonstration |
| Existing repository autoencoder / SVDD | Additional different-shape/architecture replacement | Inspect provenance, operators, dimensions and normalization first; use at least one compatible different-shape model to prove generality beyond quantizing the same network |
| Forecast model in repository | Possible future extension | Deferred until its input and output contract is established |

A single scalar model can consume one selected measurement, or a configured aggregate of several normalized measurements. It cannot directly accept the four raw AHT-temperature/AHT-humidity/distance/light values as four inputs. Demonstrate the multi-feature path with a compatible vector model separately.

For the sine demonstration, map a configured physical range to [0, 2π]: `u = clamp((value - minimum) / (maximum - minimum), 0, 1)`; `x = 2πu`. Reject `maximum <= minimum`. Example: distance 2–400 cm, replaced later by another supported scalar field with its own range. For an aggregate, explicitly configure field weights, normalization and reduction; record the final scalar in output evidence. These transforms allow numerical execution but do not make the output a meaningful sensor prediction.

Before accepting any artifact: pin upstream commit and file path; record license/attribution, SHA-256, byte size, shapes, tensor dtypes, quantization scales/zero points and operators; check the pinned runtime supports them; measure arena allocation and successful invocation on the board. Do not infer arena use from model file size. Implementation update: both pinned upstream artifacts are now bundled and have executed successfully in desktop LiteRT. Their metadata/license are in `models/hello_world`; both sine models also ran in the [ESP32-S3 capture](BOARD_VALIDATION.md).

Primary research sources (checked 3 November 2026):

- [TFLM Hello World README: existing trained model and evaluation](https://github.com/tensorflow/tflite-micro/blob/main/tensorflow/lite/micro/examples/hello_world/README.md).
- [Upstream float/int8 artifact paths](https://github.com/tensorflow/tflite-micro/blob/main/tensorflow/lite/micro/examples/hello_world/Makefile.inc).
- [Training source: scalar dense network](https://github.com/tensorflow/tflite-micro/blob/main/tensorflow/lite/micro/examples/hello_world/train.py).
- [Espressif TFLite Micro runtime](https://github.com/espressif/esp-tflite-micro). Keep this project's existing dependency pins; no runtime upgrade is required by the plan.

## Deliverable 1 — configurable sensor-to-model inputs

Pipeline: **configured bus → supported driver → decoded measurement → feature mapping/preprocessing → validated input snapshot → inference**.

| User-configurable item | Required behavior |
|---|---|
| Sensor source and driver | Select a registered driver and unique source name; reject unknown drivers |
| Bus settings | Validate relevant pins/address/mode/rate/timeout and resource conflicts using existing bus ownership rules |
| Measurement field | Select a field actually exposed by that driver, with declared units/type |
| Feature position | Bind fields explicitly to model feature order; reject duplicates, missing required positions and out-of-range indices |
| Preprocessing | Explicit scale/offset or mean/std, optional clamp and bounded reduction/window; validate all parameters |
| Sampling/freshness | Configure inference interval and maximum sample age; bounded timestamp skew for combined inputs |
| Missing data | Default: skip inference and emit a reason; optional explicit constant fill with a validity indication |

Start with the known hardware: `AHT.temperature_c`, `AHT.humidity_pct`, `LIGHT.light_lux`, `DISTANCE.distance_cm`. Names here describe the proposed measurement contract, not existing CLI syntax. Reuse numeric decoding between ML and JSON reporting.

Bus independence means adapters provide the same measurement contract over **supported** I²C, GPIO, SPI or UART drivers. An arbitrary new device still needs a driver/decoder. A raw register byte stream is not automatically a physical measurement. Publish a supported-driver matrix; never advertise every sensor on a supported bus as compatible.

Acceptance:

1. Current AHT + TSL2561 + HC-SR04+ feed a live model input path, with logged source values and transformed tensors.
2. Change a scalar binding from GPIO distance to I²C light by configuration only; inference resumes with the correct new preprocessing and no old cached samples.
3. Demonstrate another supported driver substitution, with at least one physical cross-bus swap on the available board. Use host fixtures for unavailable SPI/UART devices and label them as simulated.
4. Unknown driver/field, pin conflict, wrong units, stale/missing data and bad mapping produce explicit errors or skipped inference. Zero lux remains valid; missing readings never silently become zero.
5. Reconfiguration clears affected feature caches and applies a complete consistent configuration; unchanged acquisition and rules still pass regressions.

## Deliverable 2 — configurable model loading and execution

Package each model with a versioned manifest: model ID/version/hash, input/output shapes and dtypes, feature order and units, preprocessing, output names/interpretation, optional labels/scoring, and resource requirements. Read quantization parameters from the actual tensors and check consistency with metadata.

Initial supported envelope: one fixed-shape input tensor and one fixed-shape output tensor, float32 or int8, each at most 16 elements, supported operator set, 64 KiB arena and at most the existing 256 KiB per-model payload. Reject incompatible models explicitly. Larger tensors, dynamic shapes, unsupported/custom operators and extra tensor types require a later firmware capability change. Different models are supported within this envelope, not arbitrary neural networks.

Implement a generic raw-output/regression mode so a sine or classifier model is not forced through autoencoder MSE. Classification argmax/labels and anomaly scoring are explicit optional postprocessors, selected only when their shape/semantic contract matches. Do not report raw regression values as probabilities.

Provide a host-side package validator and an offline serial installer using the existing model-store API, with bounded chunks, checksums and acknowledgements. Do not erase firmware, NVS or unrelated partitions. Reuse the existing MQTT installer later; a firmware rebuild for every model does not meet runtime replacement acceptance.

Validate package, operators, tensors, feature coverage and resource limits before activation. Pause inference during replacement, clear prior outputs/preprocessing, bind the manifest to the exact model hash and configuration generation, and perform a test invocation. On failure restore the previous working model/configuration or report a clearly disabled state; never mix old metadata with new weights. Current `engine_load` unloads first, so storage's two slots alone do not establish interpreter rollback. Persist model and matching manifest together and define restart behavior.

Acceptance:

1. Install/run pretrained float sine, then int8 sine using the same firmware; compare known test vectors against desktop inference with documented tolerance.
2. Install/run a compatible different-shape model (for example the existing autoencoder after inspection), supply its complete feature mapping, and show actual outputs. This validates more than a dtype change.
3. Reject damaged files, wrong manifest/hash, unsupported operators/type/shape, invalid quantization and insufficient arena without crashing acquisition.
4. Verify failed activation and interrupted transfer recovery; reset must restore a valid matching package or explicitly require configuration, never silently use stale settings.
5. Log model ID/hash, configuration generation, timestamps, input vector, outputs, status, invoke time and arena usage. Outputs must be attributable to an actual successful invocation.

## Delivery sequence and evidence

| Milestone | Deliverables | Completion gate |
|---|---|---|
| M0: inspect and pin models | Artifact inventory, licenses, manifest examples, desktop vectors | Actual artifacts inspected; compatibility recorded |
| M1: sensor adaptation | Typed measurement API, feature builder, configuration validation and presets | Cross-bus binding change plus fault tests pass |
| M2: model replacement | Manifest validator, serial installer, generic outputs, safe activation | Float/int8 and different-shape models invoke on target |
| M3: offline integration | Repeatable VS Code instructions, host tests and board captures | Three-sensor regression, configuration/model swaps and ten-minute stability run |
| M4: MQTT follow-up | Publish results/config generation/model identity; reuse control/upload path | Broker disconnect/reconnect and overload do not halt local inference |

M1/M2 require host tests for mappings, quantization, serialization and lifecycle plus real ESP32-S3 evidence for invocation and sensor swaps. Record stack headroom, memory use and invocation timings for feasibility, without a speed/accuracy optimization target. Keep acquisition, rule processing and watchdog service operational; interpreter access remains owned by InferenceTask.

For M4, retain existing raw sensor topics. Add versioned ML result payloads and bounded queues using the existing MQTTTask. Network callbacks must not mutate interpreter/configuration state directly. Offline inference remains operational without Wi-Fi. MQTT and the LCD do not block M0–M3.

Proposed artifacts: this plan; a supported-driver/model matrix; versioned model manifests and feature presets; licensed pinned model assets; host validation/install tools; automated tests; `VALIDATION.md` with commands, firmware/model hashes and real board captures. Any proposed configuration examples must be marked provisional until their parser exists.

## Definition of done for the first two deliverables

Using one firmware image, demonstrate **sensor configuration A → configuration B across supported buses**, then **model A → model B with matching manifests**, while producing valid, traceable inference outputs and handling incompatible settings cleanly. Include a multi-feature model demonstration. No claim of people-flow detection, classification accuracy or environmental anomaly validity follows from these execution tests.
