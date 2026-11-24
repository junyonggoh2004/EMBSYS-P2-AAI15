# Live People-Counter and three-board restoration — 24 November 2026

Board B (`s3-002`, ESP32-S3 N16R8) was rebuilt and flashed on COM11. ESP-IDF reported `Hash of data verified`. The four bundled D6 demo models remain in the image. Board A and C firmware were not changed in this session.

The isolated test loaded [`board_a_people_counter.cfg`](../../config/board_a_people_counter.cfg) and [`board_b_people_counter.cfg`](../../config/board_b_people_counter.cfg), then the existing [`board_b_people_counter.json`](../../config/board_b_people_counter.json). Both board configs returned `OK`, and B accepted the model with `{"ok":true,"reason":"applied"}`. A published valid Sonar A echo frames with source timestamps about 70 ms apart. B received the typed remote stream and published this live result to `pico/s3-002/ml/result`:

```json
{"type":"ml","model":"people-counter","input_source":"remote:s3-001","outputs":[2],"direction":"out","class_score":0.9960938,"status":"OK"}
```

The boards were on a desk, not mounted at a doorway. This result verifies the live frame-to-inference-to-MQTT path. It does not establish that `out` correctly describes a person exiting. The source model was trained near 50 ms sampling, whereas this profile uses 70 ms; preprocessing and labelled IN/NONE/OUT crossings remain to be validated. Predictions do not change occupancy or Board C's LCD.

B initially measured the arrival spacing of MQTT frames. Wi-Fi sometimes delivered several correctly spaced source frames in a short burst, clearing the 31-reading window. The isolated model path now checks A's timestamp for spacing and B's clock for staleness, and ignores a duplicate source timestamp. It also consumes the high-rate remote frame without republishing its redundant B alias telemetry, reserving B's MQTT queue for model results. Other model and normal sensor bridges keep their previous behavior. A host regression covers a 70 ms source stream arriving in 5 ms bursts; the N16R8 firmware build and flash passed.

After the test, the normal [`board_a.cfg`](../../config/board_a.cfg), [`board_b_ml.cfg`](../../config/board_b_ml.cfg), and default [`board_b_sine_float.json`](../../config/board_b_sine_float.json) were reloaded. B again published changing `d6/board-b/snapshot` messages with temperature, humidity, light, and separate Sonar A/B distances, plus live `sine-float` results with `status:OK`. Board C's horizontal LCD was visually confirmed updating. One snapshot reported Sonar B as `-1.0`, the documented missing-echo value; it is not a fabricated distance.

Use [ML_commands.md](../commands/ML_commands.md) for the isolated model procedure and [usecase_commands.md](../commands/usecase_commands.md) to restore the normal four-sensor A→B→C setup. No Wi-Fi credentials are recorded here.
