#!/bin/sh
# Native parser/bridge and LCD HAL tests; no physical display or target evidence.
set -eu
OUT_DIR=$(mktemp -d "${TMPDIR:-/tmp}/d6-test.XXXXXX")
trap 'rm -rf "$OUT_DIR"' EXIT HUP INT TERM
${CC:-cc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror -I libraries/EdgeCore/src \
  tests/test_sensor_frame.c libraries/EdgeCore/src/mqtt/sensor_frame.c \
  libraries/EdgeCore/src/application/sensor_measurement.c -o "$OUT_DIR/frame"
"$OUT_DIR/frame"
${CC:-cc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror -I libraries/EdgeCore/src \
  tests/test_sensor_bridge.c libraries/EdgeCore/src/mqtt/sensor_bridge.c \
  libraries/EdgeCore/src/mqtt/sensor_frame.c -o "$OUT_DIR/bridge"
"$OUT_DIR/bridge"
LCD_DIR=esp32s3_mtk/components/lcd_output
${CC:-cc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror \
  -I "$LCD_DIR/include" -I "$LCD_DIR" -I libraries/EdgeCore/src \
  "$LCD_DIR/lcd_output.c" "$LCD_DIR/lcd_st7789.c" \
  "$LCD_DIR/lcd_transport_mtk.c" "$LCD_DIR/lcd_font_5x7.c" \
  "$LCD_DIR/tests/test_lcd_output.c" -o "$OUT_DIR/lcd"
"$OUT_DIR/lcd"
python3 -m unittest discover -s tests -p test_mtk_check.py
