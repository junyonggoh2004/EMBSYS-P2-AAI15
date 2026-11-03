# ESP32-S3 Super Mini sensor target

This directory is a separate ESP-IDF application so the existing Pico SDK
target remains buildable. It targets a generic ESP32-S3 Super Mini and routes
the console over native USB Serial/JTAG.

## Build

Use ESP-IDF 5.2 or newer (the code uses the current I2C master-bus API):

```sh
cd esp32s3
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Physical bus and pin choices are in `main/include/hardware_config.h`, with the
defaults instantiated in `main/app_main.c`. Sensor/panel behavior is separately
defined by `device_config_t` in `main/include/device_config.h`.

The common registry records every GPIO, ADC, I2C bus/device, and SPI bus/device
claim. It rejects duplicate pins and board-reserved GPIO0, GPIO19, GPIO20, and
GPIO48 before any driver is initialized.

## Safety and status behavior

- Supply every external module from 3V3 unless its breakout documentation
  explicitly requires otherwise, and connect all grounds together. ESP32-S3
  GPIO is 3.3 V-only.
- `backlight_is_logic_input` defaults to `false`; GPIO5 is neither claimed nor
  driven until the breakout's BL pin is confirmed to be a logic input.
- HC-SR04 support defaults to disabled. Do not connect Echo directly. The code
  refuses to claim or initialize GPIO2/GPIO13 unless both `enabled` and
  `echo_level_shifter_installed` are true after a 5 V-to-3.3 V divider or level
  shifter has been installed.
- Reads return a status with their data. I2C errors, ADC errors, optical
  saturation, disabled devices, and ultrasonic timeouts never become valid zero
  measurements.
- The TFT is write-only in this configuration. Its successful writes verify
  only the ESP32 transmit path; they are not an SPI receive-integrity test.

## Panel configuration

`device_config_t.tft` selects `TFT_CONTROLLER_ST7789P3` or
`TFT_CONTROLLER_NV3023B` and controls offsets, 0/90/180/270-degree rotation,
RGB/BGR order, inversion, visible size, and optional logic-level backlight.
The defaults are 76x284, RGB565, ST7789P3, mode 0, 10 MHz, and the commonly
used ST7789P3 RAM offsets x=82/y=18. Offsets remain configuration values because
they can differ by module and rotation.

Panel glass can require vendor-specific power/gamma values and non-zero RAM
offsets. Set the offsets and controller for the exact breakout before use; the
NV3023B hook intentionally uses only its portable DCS startup commands until a
module-specific initialization table is known.

The published 76x284 breakout documentation identifies ST7789P3. The available
NV3023B controller datasheet describes a different native geometry, so treat the
NV3023B selector as an integration hook only unless the exact breakout vendor
provides a matching initialization table and RAM geometry.
