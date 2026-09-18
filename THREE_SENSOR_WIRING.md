# Three-sensor wiring: Pico 2 W

Flash only `build/multi_sensor_monitor.uf2`. A Pico has one running firmware
image; copying a new UF2 replaces the previous one.

## 1. Grove Digital Light Sensor v1.1 (TSL2561)

| Grove wire | Pico pin | Physical pin |
|---|---|---:|
| Red | 3V3(OUT) | 36 |
| Black | GND | 38 |
| White (SDA) | GP16 | 21 |
| Yellow (SCL) | GP17 | 22 |

## 2. HC-SR04 ultrasonic distance sensor

The HC-SR04 is not normally Grove-compatible. Use Grove-to-Dupont leads or a
Grove breakout and follow the labels printed on the HC-SR04 board.

| HC-SR04 pin | Pico pin | Physical pin |
|---|---|---:|
| VCC | VBUS (5 V from USB) | 40 |
| GND | GND | 38 |
| TRIG | GP18 | 24 |
| ECHO | GP19 through a voltage divider | 25 |

**Important:** HC-SR04 `ECHO` is 5 V when the sensor is powered at 5 V. Do not
connect it directly to GP19. Wire a 1 kOhm resistor from `ECHO` to GP19 and a
2 kOhm resistor from GP19 to GND. This reduces the 5 V signal to about 3.3 V,
which is safe for the Pico.

For a four-wire Grove adapter, use yellow for `TRIG` and white for `ECHO` only
if those wires map to the labelled pins through the adapter; colours do not
replace the labels on the HC-SR04 board.

## 3. TCRT5000 infrared reflective line-tracking module

This module detects the reflected infrared light from a nearby surface. Its
potentiometer sets the black/white surface switching threshold. It does not
measure temperature.

| Module pin / Grove wire | Pico pin | Physical pin |
|---|---|---:|
| VCC / red | 3V3(OUT) | 36 |
| GND / black | GND | 38 |
| DO or OUT / yellow | GP20 | 26 |
| White | Leave disconnected |

Hold the sensor over the line/background and turn its potentiometer until its
indicator LED changes. The serial monitor reports `line: DETECTED` when its
digital output is active. If detected/not detected is reversed for your board,
change `TCRT5000_ACTIVE_LEVEL` from `0` to `1` in the source and rebuild.

## Expected serial output

```text
TSL2561 ready (ID=0x50)
light: 123.4 lux  distance: 42.7 cm  line: DETECTED
```
