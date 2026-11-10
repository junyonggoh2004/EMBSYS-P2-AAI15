# D4 Part 1 ESP32-S3 evidence — 10 November 2026

The ESP32-S3 on COM5 ran the complete three-sensor preset and four consecutive model selections on one firmware image. Flash verification passed. Each stage captured valid AHT, TSL2561 and HC-SR04+ readings and three successful inferences. The board returned to `sine-float` with all three sensors enabled at the end. [Raw serial capture](../evidence/d4-model-swap-20261110.log)

Firmware SHA-256 `40524848894429fe8b432dae4ad867d1e6686a6ee9a2d9e8138d2ba9626958a3`. The trace build uses the branch's pinned ESP-IDF and µT-Kernel dependencies, Espressif TFLite Micro 1.4.0 and ESP-NN 1.4.1.

| Model | Input → output elements | Successful outputs | Board uptime (ms) | First invocation vector calls | Max difference vs reference |
|---|---:|---:|---:|---:|---:|
| `sine-float` | 1 → 1 | 3 | 19828–29828 | 0 | 4.14e-07 |
| `sine-int8` | 1 → 1 | 3 | 31868–37868 | 2 | 0.00829 |
| `autoencoder` | 16 → 16 | 3 | 39899–51899 | 0 | 4.62e-07 |
| `sine-float` | 1 → 1 | 3 | 61934–65934 | 0 | 6.1e-07 |

The output check independently evaluated the captured inputs using the TFLite weights, biases, activations and tensor scales with NumPy. All 12 outputs agreed within the tolerance of `1e-5` for float and one output quantization step plus `1e-5` for INT8. This is a correctness check on the model execution.

## Acceleration evidence

The three INT8 `FULLY_CONNECTED` operations in `sine-int8` use input widths 1, 16 and 16. Espressif's TFLite Micro dispatches the INT8 operations through ESP-NN. For the aligned, short rows here, ESP-NN uses its `esp_nn_fc_s16_esp32s3` assembly path. The wrapper counts completed calls with a row length of at least eight and at least one output channel. The pinned assembly enters the signed vector MAC loop for those inputs. The board reported two such calls in the first successful `sine-int8` invocation. The counter covers this particular fully connected assembly path.

The S3-specific objdump output from the flashed ELF shows the wrapper's call and the vector MAC in the real kernel.

```text
42011cd8 <__wrap_esp_nn_fc_s16_esp32s3>:
42011d0d: 52c525  call8 42064960 <esp_nn_fc_s16_esp32s3>
42064960 <esp_nn_fc_s16_esp32s3>:
42064a03: 1a1184  ee.vmulas.s16.accx q1, q2
```

## Repeat the local demo

1. Build and flash from `dev-jy-ml`, then run **MTK: load sensors and ML** and **MTK: monitor raw**.
2. Wait for valid readings from all three sensors and numeric ML output.
3. Send `STOP`, `ML PRESET sine-int8`, `ML RUN`, then `RUN`, one line at a time. Record the `[ML-ACCEL]` line and the next successful ML JSON.
4. Repeat the sequence with `ML PRESET autoencoder` and then `ML PRESET sine-float`. Check the model name, tensor sizes and increasing timestamps.

The demo pauses sampling with `STOP` for each swap. Continuous sampling during reconfiguration, model activation over MQTT, remote inputs and MQTT output routing remain integration tasks for D4 Part 2.
