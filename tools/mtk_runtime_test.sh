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
  -I esp32s3_mtk/components/lcd_output/include \
  tests/test_mtk_application_runtime.c \
  libraries/EdgeCore/src/application/application_dispatch.c \
  libraries/EdgeCore/src/application/application_state.c \
  libraries/EdgeCore/src/application/event_record.c \
  libraries/EdgeCore/src/mqtt/sensor_frame.c \
  -o "$FIXTURE/runtime-test"
"$FIXTURE/runtime-test"

# D1 task 1.4: the console and the MQTT command topic share repl_command().
${CC:-cc} -std=c11 -Wall -Wextra -Werror \
  -I libraries/EdgeCore/src \
  tests/test_repl_commands.c libraries/EdgeCore/src/application/repl.c \
  -o "$FIXTURE/repl-commands-test"
"$FIXTURE/repl-commands-test"

# D1 task 1.2: cfg.to/cmd.to payloads are not checked against this board's pins.
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-error=switch \
  -I libraries/EdgeCore/src \
  tests/test_rule_actions.c libraries/EdgeCore/src/rule_engine/rules.c \
  libraries/EdgeCore/src/rule_engine/rule_expr.c -lm \
  -o "$FIXTURE/rule-actions-test"
"$FIXTURE/rule-actions-test"

# D1 task 1.2 / D-11: bare-word JSON (the reference cfg.to example) converts like strict JSON.
# The reference parser's own warnings are reported but do not fail the build.
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-error=unused-parameter -Wno-error=sign-compare \
  -Wno-error=unused-but-set-variable -I libraries/EdgeCore/src \
  tests/test_json_relaxed.c libraries/EdgeCore/src/mqtt/mqtt_json_parser.c \
  libraries/EdgeCore/src/third_party/jsmn/jsmn.c -o "$FIXTURE/json-relaxed-test" 2>/dev/null
"$FIXTURE/json-relaxed-test" | tail -1
