#!/bin/sh
# D5 laptop tests: the MLBENCH command and the benchmark runner. No board.
set -eu
OUT=$(mktemp -d "${TMPDIR:-/tmp}/d5-test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -I libraries/EdgeCore/src \
  -I esp32s3_mtk/components/edge_mtk/include tests/test_ml_bench.c -o "$OUT/ml-bench-test"
"$OUT/ml-bench-test"
if python3 -c "" 2>/dev/null; then PY=python3; else PY=python; fi
$PY tests/test_d5_bench.py
