#!/bin/sh
# Every file in config/ and the multi-bus config lines in README 5.4 go through
# the board's own config-topic path (mqtt_config_parser.c and config_parser.c)
# and must be accepted with the right pins, buses and timing; broken lines must
# be refused. D1 task 1.3, D6 tasks 5.3, 5.4 and 5.10. No board needed.
set -eu
OUT=${TMPDIR:-/tmp}/config_files_test
# The reference JSON parser's own warnings are reported but do not fail the build,
# as in tools/mtk_runtime_test.sh.
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-error=unused-parameter -Wno-error=sign-compare \
  -Wno-error=unused-but-set-variable -I libraries/EdgeCore/src \
  tests/test_config_files.c \
  libraries/EdgeCore/src/mqtt/mqtt_config_parser.c \
  libraries/EdgeCore/src/application/config_parser.c \
  libraries/EdgeCore/src/application/timing_contract.c \
  libraries/EdgeCore/src/application/repl.c \
  libraries/EdgeCore/src/mqtt/mqtt_json_parser.c \
  libraries/EdgeCore/src/third_party/jsmn/jsmn.c \
  -o "$OUT"
"$OUT"
