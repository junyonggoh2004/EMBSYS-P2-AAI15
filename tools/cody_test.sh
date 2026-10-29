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

GPIO_OUT=${TMPDIR:-/tmp}/cody_gpio_pulse_config_tests
cc -std=c11 -Wall -Wextra -Werror \
  -I libraries/EdgeCore/src \
  tests/test_gpio_pulse_config.c \
  libraries/EdgeCore/src/bus/gpio.c \
  libraries/EdgeCore/src/application/config_parser.c \
  libraries/EdgeCore/src/application/timing_contract.c \
  -o "$GPIO_OUT"
"$GPIO_OUT"

SAFETY_OUT=${TMPDIR:-/tmp}/cody_event_safety_tests
cc -std=c11 -Wall -Wextra -Werror \
  -I libraries/EdgeCore/src \
  tests/test_event_safety.c \
  libraries/EdgeCore/src/application/event_record.c \
  libraries/EdgeCore/src/application/application_state.c \
  -o "$SAFETY_OUT"
"$SAFETY_OUT"

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

PROVENANCE_OUT=${TMPDIR:-/tmp}/cody_rule_provenance_tests
cc -std=c11 -Wall -Wextra -Werror \
  -I libraries/EdgeCore/src \
  tests/test_rule_event_provenance.c \
  libraries/EdgeCore/src/rule_engine/rule_action.c \
  libraries/EdgeCore/src/application/event_record.c \
  libraries/EdgeCore/src/application/application_state.c \
  libraries/EdgeCore/src/application/application_dispatch.c \
  -o "$PROVENANCE_OUT"
"$PROVENANCE_OUT"

# Test the target runner's parsing/failure behavior without pyserial or a board.
if command -v python3 >/dev/null 2>&1; then SMOKE_PY=python3; else SMOKE_PY=python; fi
"$SMOKE_PY" -m unittest discover -s tests -p test_application_smoke.py

# Execute actual application/control runtime bodies with test-only kernel/SDK seams.
sh tools/mtk_runtime_test.sh
