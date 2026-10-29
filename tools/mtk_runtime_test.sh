#!/bin/sh
# Native runtime integration with test-only kernel/SDK boundaries; no target ABI proof.
set -eu
FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/d3-runtime-test.XXXXXX")
trap 'rm -rf "$FIXTURE"' EXIT HUP INT TERM
mkdir -p "$FIXTURE/tk"
printf '#include "mtk_runtime_test.h"\n' > "$FIXTURE/tk/tkernel.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-error=comment \
  -DEDGE_MTK_RUNTIME_TEST \
  -I "$FIXTURE" -I tests -I libraries/EdgeCore/src \
  -I esp32s3_mtk/components/edge_mtk/include \
  tests/test_mtk_application_runtime.c \
  libraries/EdgeCore/src/application/application_dispatch.c \
  libraries/EdgeCore/src/application/application_state.c \
  libraries/EdgeCore/src/application/event_record.c \
  -o "$FIXTURE/runtime-test"
"$FIXTURE/runtime-test"
