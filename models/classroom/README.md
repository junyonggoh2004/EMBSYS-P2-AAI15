# Classroom model assets

`comfort_summer.tflite` is the published FP32 checkpoint from jbonett's Apache-2.0 thermal comfort project. Its input is `[temperature_c, humidity_pct]`, shape `[1,2]`, and its output is 100 class scores, shape `[1,100]`. The original firmware uses the winning class index as the comfort score. The file is 5,960 bytes with SHA-256 `41f80cb290e572fde7d7010fa93fed0659339c0eb38692957b38ab40e2bfdd31`. [Pinned source](https://github.com/jbonett/thermal-comfort-tflite/tree/d146711ee926a0a83cedf555b7604472efc4ca1f) · [License](LICENSE)

Add your permitted local INT8 export of Edge Impulse project 18808 as `people_counter_raw_int8.tflite` to opt in to the third classroom model. The exported weights are supplied locally. [Public project](https://studio.edgeimpulse.com/public/18808/latest) · [Run guide](../../docs/embedded-ml/RUNNING.md#switch-between-the-classroom-demo-models)
