# LCD hardware image index

These images are stored with the `lcd_output` component so the test guide does
not depend on chat attachments or temporary files. The breadboard photograph
is **empty**; it shows the user's board and its printed rows, not completed
wiring. Use the [LCD-only wiring table](../../../WIRING.md#separate-lcd-only-breadboard-test-current-lcd_output-firmware)
and [breadboard schematic](../LCD_BREADBOARD.svg) for the actual connections.

| Image | What it documents |
|---|---|
| [ESP32-S3 Super Mini pinout](esp32s3-super-mini-pinout.jpg) | Existing project pinout reference showing the 3V3, GND and GPIO8–12 header labels. Confirm against the labels printed on the user's board. |
| [Empty breadboard](breadboard-empty.jpg) | User-provided photo showing the blue ground rail and A–E/F–J terminal strips before wiring. |
| [LCD module back](lcd-module-back.jpg) | User-provided photo of the breakout, marked ST7789P3 and 76×284. |
| [LCD package label](lcd-package-label.jpg) | User-provided DIYUSER `M35-2.25TFT-lanban` product label. |
| [First test, inverted colours](lcd-test-inverted.jpg) | User-provided screen photo after `LCDTEST 76 284 82 18 00 1`; visible bands and text, colours inverted. |
| [Working RGB test](lcd-test-rgb.jpg) | User-provided screen photo after `LCDTEST 76 284 82 18 00 0`; intended bands and `LCD READY`. |
| [Flash JEDEC ID](flash-spiid-ef4016.png) | User-provided serial capture of read-only `SPIID` reporting `EF4016` with the flash and LCD connected. |
| [Combined flash/LCD result](flashlcd-result.png) | User-provided serial capture of `FLASHLCD` reporting flash ID `EF4016`, flash result `0`, and LCD result `0`. The user separately confirmed the ID was visible on screen. |
| [Combined test task headroom](flashlcd-tasks.png) | User-provided `TASKS` capture after `FLASHLCD`: `BenchmarkTask stack_free=1756 B`, message-buffer drops `0`. |

The successful photo establishes visible display output, while the serial
capture recorded in the [component guide](../README.md) establishes
`[LCD] test complete result=0`. The photos do not show all wired breadboard
connections or prove every edge pixel is aligned.
