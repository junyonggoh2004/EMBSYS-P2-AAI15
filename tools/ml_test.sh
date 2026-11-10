#!/bin/sh
set -eu
OUT=${TMPDIR:-/tmp}/sensor_ml_tests
${CC:-cc} -std=c11 -Wall -Wextra -Werror -I libraries/EdgeCore/src -I esp32s3_mtk/components/edge_mtk/include \
 tests/test_sensor_ml.c libraries/EdgeCore/src/application/sensor_measurement.c -lm -o "$OUT"
"$OUT"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-error=comment -I libraries/EdgeCore/src \
 tests/test_ml_manager.c libraries/EdgeCore/src/inference_engine/inference_manager.c -lm -o "$OUT"
"$OUT"

${CC:-cc} -std=c11 -O2 -flto -fwhole-program -I libraries/EdgeCore/src \
 tests/test_repl_input.c libraries/EdgeCore/src/application/repl.c -o "$OUT"
"$OUT"
