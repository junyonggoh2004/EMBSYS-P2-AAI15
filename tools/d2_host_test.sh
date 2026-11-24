#!/bin/sh
set -eu
cc -std=c11 -Wall -Wextra -Werror -I libraries/EdgeCore/src \
  tests/test_w25q32.c libraries/EdgeCore/src/flash/w25q32.c \
  -o "${TMPDIR:-/tmp}/d2-w25q32-test"
"${TMPDIR:-/tmp}/d2-w25q32-test"

# D2-8: events processed by the application logic are mapped, stored, recovered and read back.
cc -std=c11 -Wall -Wextra -Werror -I libraries/EdgeCore/src \
  tests/test_event_log.c libraries/EdgeCore/src/application/event_log_map.c \
  libraries/EdgeCore/src/application/application_state.c \
  libraries/EdgeCore/src/application/event_record.c \
  libraries/EdgeCore/src/flash/w25q32.c -lm \
  -o "${TMPDIR:-/tmp}/d2-event-log-test"
"${TMPDIR:-/tmp}/d2-event-log-test"

# D2-2 and D2-4: the real SPI bus driver against a modelled HAL and W25Q32.
cc -std=c11 -Wall -Wextra -Werror -I libraries/EdgeCore/src \
  tests/test_spi_driver.c libraries/EdgeCore/src/bus/spi.c \
  -o "${TMPDIR:-/tmp}/d2-spi-driver-test"
"${TMPDIR:-/tmp}/d2-spi-driver-test"
