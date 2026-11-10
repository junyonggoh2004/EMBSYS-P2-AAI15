#!/bin/sh
set -eu
OUT=${TMPDIR:-/tmp}/sensor_profile_tests
${CC:-cc} -std=c11 -Wall -Wextra -Werror -I libraries/EdgeCore/src \
 tests/test_i2c_profiles.c libraries/EdgeCore/src/bus/i2c.c \
 libraries/EdgeCore/src/bus/i2c_profiles.c libraries/EdgeCore/src/application/config_parser.c \
 libraries/EdgeCore/src/application/timing_contract.c libraries/EdgeCore/src/application/sensor_measurement.c \
 libraries/EdgeCore/src/application/scheduler.c libraries/EdgeCore/src/application/output_format.c \
 libraries/EdgeCore/src/application/entrance_window.c libraries/EdgeCore/src/application/event_identity.c \
 libraries/EdgeCore/src/application/ld2450.c libraries/EdgeCore/src/application/event_record.c \
 libraries/EdgeCore/src/application/pulse_sequence.c \
 -lm -o "$OUT"
"$OUT"
