#!/bin/sh
set -eu
OUT=${TMPDIR:-/tmp}/edge_startup_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -DEDGE_STARTUP_TEST \
 -I tests -I esp32s3_mtk/main tests/test_startup.c -o "$OUT"
"$OUT"
