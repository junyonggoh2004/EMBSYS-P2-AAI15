#!/bin/sh
set -eu
FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/d6-live-test.XXXXXX")
trap 'rm -rf "$FIXTURE"' EXIT HUP INT TERM
mkdir -p "$FIXTURE/tk"
printf '#include "mtk_runtime_test.h"\n' > "$FIXTURE/tk/tkernel.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror \
  -I "$FIXTURE" -I tests -I libraries/EdgeCore/src \
  -I esp32s3_mtk/components/edge_mtk/include \
  tests/test_d6_live.c \
  esp32s3_mtk/components/edge_mtk/d6_live.c \
  libraries/EdgeCore/src/application/sensor_measurement.c \
  -o "$FIXTURE/d6-live-test"
"$FIXTURE/d6-live-test"
