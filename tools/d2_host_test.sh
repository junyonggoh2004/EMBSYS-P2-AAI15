#!/bin/sh
set -eu
cc -std=c11 -Wall -Wextra -Werror -I libraries/EdgeCore/src \
  tests/test_w25q32.c libraries/EdgeCore/src/flash/w25q32.c \
  -o "${TMPDIR:-/tmp}/d2-w25q32-test"
"${TMPDIR:-/tmp}/d2-w25q32-test"
