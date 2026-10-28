#!/bin/sh
set -eu

OUT=${TMPDIR:-/tmp}/cody_contract_tests
cc -std=c11 -Wall -Wextra -Werror \
  -I libraries/EdgeCore/src \
  tests/test_cody_contracts.c \
  libraries/EdgeCore/src/application/timing_contract.c \
  libraries/EdgeCore/src/application/event_record.c \
  libraries/EdgeCore/src/application/application_state.c \
  libraries/EdgeCore/src/application/config_parser.c \
  -o "$OUT"
"$OUT"

DISPATCH_OUT=${TMPDIR:-/tmp}/cody_dispatch_tests
cc -std=c11 -Wall -Wextra -Werror \
  -I libraries/EdgeCore/src \
  tests/test_application_dispatch.c \
  libraries/EdgeCore/src/application/application_dispatch.c \
  libraries/EdgeCore/src/application/application_state.c \
  libraries/EdgeCore/src/application/event_record.c \
  -o "$DISPATCH_OUT"
"$DISPATCH_OUT"

SCHEDULER_OUT=${TMPDIR:-/tmp}/cody_scheduler_tests
cc -std=c11 -Wall -Wextra -Werror -DSCHED_DEBUG=0 \
  -I libraries/EdgeCore/src \
  tests/test_scheduler_timing.c \
  libraries/EdgeCore/src/application/scheduler.c \
  libraries/EdgeCore/src/application/timing_contract.c \
  libraries/EdgeCore/src/application/output_format.c \
  -o "$SCHEDULER_OUT"
"$SCHEDULER_OUT"
