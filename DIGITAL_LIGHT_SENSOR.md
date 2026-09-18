# Digital light sensor with Pico 2 W

This standalone firmware reads the Grove - Digital Light Sensor v1.1. It uses
the module's built-in TSL2561 I2C light-to-digital converter and prints a light
level in lux over USB serial. It does not require Wi-Fi or MQTT credentials.

## Wiring

Power the sensor from **3.3 V**, not 5 V, so its digital output is safe for the
Pico 2 W.

| Light sensor | Pico 2 W | Physical pin |
|---|---|---:|
| `VCC` / `+` | `3V3(OUT)` | 36 |
| `GND` / `-` | `GND` | 38 (or any GND) |
| White / `SDA` | `GP16` | 21 |
| Yellow / `SCL` | `GP17` | 22 |

The Grove sensor needs all four wires. There is no trim pot or separate `DO`
pin to configure: the light measurement is sent through the SDA and SCL wires.

## Flash and observe

1. Hold **BOOTSEL** while connecting the Pico 2 W to the computer.
2. Copy `build/digital_light_sensor.uf2` to the mounted `RP2350` drive.
3. Open the Pico's USB serial port with any terminal. The baud rate is ignored
   for USB CDC; `115200` is a convenient choice.

The firmware prints one line each second and immediately after a stable state
change:

```text
TSL2561 detected (ID=0x50)
light: 123.4 lux  (full=456 IR=78)
```

If the sensor is not found, the firmware prints the wiring to check. The module
address is `0x29`; no manual I2C setup is required beyond the four connections.

## Build only this firmware

Configure the repository for `pico2_w` as described in `SETUP.md`, then run:

```sh
cmake --build build --target digital_light_sensor
```

To use another I2C0 GPIO pair, update `I2C_SDA_GPIO` and `I2C_SCL_GPIO` near
the top of `project_v3/digital_light_sensor.c`. The pins must form a valid I2C
pair (for example GP16/GP17 or GP20/GP21).
