# Pico 2 W sensor wiring reference

This document records the wiring plan for the Raspberry Pi Pico 2 W and these
four sensors:

1. Grove - Digital Light Sensor v1.1 (`TSL2561`)
2. `HC-SR04` ultrasonic distance sensor
3. `TCRT5000` infrared reflective line-tracking sensor
4. `AHT10` temperature and humidity sensor

The pin numbers in this document are **physical pins on the Pico 2 W's
40-pin header**, not just GPIO numbers. `GP16`, for example, is physical pin
21. The USB serial monitor is used for output; none of these sensors use a
UART/serial port.

## Pin assignment summary

| Sensor | Sensor connection | Pico GPIO | Physical pin | Interface |
|---|---|---:|---:|---|
| TSL2561 light | SDA | GP16 | 21 | I2C0 |
| TSL2561 light | SCL | GP17 | 22 | I2C0 |
| AHT10 | SDA | GP16 | 21 | I2C0, shared with TSL2561 |
| AHT10 | SCL | GP17 | 22 | I2C0, shared with TSL2561 |
| HC-SR04 | TRIG | GP18 | 24 | GPIO output |
| HC-SR04 | ECHO (after level shifting) | GP19 | 25 | GPIO input |
| TCRT5000 | D0 / DO | GP20 | 26 | GPIO input |

The TSL2561 and AHT10 intentionally share GP16 and GP17. I2C is a bus, so
multiple I2C devices can share SDA and SCL as long as their addresses differ:

| Device | I2C address |
|---|---:|
| Grove Digital Light Sensor v1.1 / TSL2561 | `0x29` |
| AHT10 | `0x38` |

## Power and ground rules

- **Pin 36 (`3V3(OUT)`)** powers the TSL2561, AHT10, and TCRT5000.
- **Pin 37 (`3V3_EN`) is not a power output. Do not connect a sensor to it.**
- The HC-SR04 needs 5 V on **pin 40 (`VBUS`)** when the Pico is powered over
  USB.
- All sensors must share a common ground with the Pico.
- One physical header pin cannot safely hold multiple jumper ends. To power
  more than one 3.3 V sensor, use a breadboard power rail, a 1-to-2/1-to-3
  Dupont splitter, a Grove splitter, or a small terminal block.

Suggested ground pins are physical pins 3, 8, 13, 18, 23, 28, 33, and 38. Any
of these may be used; they are connected together on the Pico.

## 1. Grove Digital Light Sensor v1.1 (TSL2561)

This is an I2C lux sensor. On a standard Grove I2C cable, the colours map as
shown below.

| Grove wire | Function | Pico 2 W connection |
|---|---|---|
| Red | VCC | 3V3(OUT), physical pin 36 |
| Black | GND | GND, physical pin 38 or another GND pin |
| White | SDA | GP16, physical pin 21 |
| Yellow | SCL | GP17, physical pin 22 |

No external resistor is required for normal short Grove cables: the sensor
breakout provides the required I2C pull-up resistors. The TSL2561 uses address
`0x29`. See [Seeed's Grove Digital Light Sensor documentation](https://wiki.seeedstudio.com/Grove-Digital_Light_Sensor/).

## 2. HC-SR04 ultrasonic distance sensor

The normal HC-SR04 module is not a Grove device. Use female-to-female Dupont
wires, matching the labels printed on the sensor board, normally ordered
`VCC`, `TRIG`, `ECHO`, `GND` when viewed from the front with its two round
transducers facing you.

| HC-SR04 pin | Pico 2 W connection |
|---|---|
| VCC | VBUS 5 V, physical pin 40 |
| GND | GND, physical pin 38 or another GND pin |
| TRIG | GP18, physical pin 24 |
| ECHO | GP19, physical pin 25, **only through the divider below** |

### Required HC-SR04 Echo voltage divider

The HC-SR04 is powered at 5 V and therefore sends a 5 V signal on `ECHO`.
Pico GPIO pins are 3.3 V only. **Never connect ECHO directly to GP19.**

Use two resistors:

```text
HC-SR04 ECHO ── 1 kOhm ──+── GP19 (physical pin 25)
                          |
                        2 kOhm
                          |
                         GND
```

The divider reduces the 5 V ECHO pulse to approximately 3.3 V. A 10 kOhm and
20 kOhm pair works equivalently. The HC-SR04 expects a 10 microsecond trigger
pulse and returns an Echo pulse proportional to distance. [HC-SR04 reference](https://solderhub.com/sensors/hc-sr04)

## 3. TCRT5000 reflective line sensor

The TCRT5000 senses reflected infrared light to distinguish a dark line from a
light background. It is **not a temperature sensor**. The common module has
`VCC`, `GND`, `D0`/`DO`, and optional `A0` pins.

| TCRT5000 pin | Pico 2 W connection |
|---|---|
| VCC | 3V3(OUT), physical pin 36, via the shared 3.3 V rail/splitter |
| GND | Any Pico GND pin |
| D0 / DO | GP20, physical pin 26 |
| A0 | Leave disconnected for the current digital line-detection firmware |

No external resistor or level converter is needed when the module is powered
from 3.3 V: its `D0` output is then safe for the Pico. Adjust the module's blue
potentiometer while holding it 2–5 mm above black and white surfaces until its
status LED changes. If the output logic is reversed, firmware can invert it.

## 4. AHT10 temperature and humidity sensor

The AHT10 is an I2C temperature and humidity sensor. It shares the same I2C
bus as the TSL2561.

| AHT10 pin | Pico 2 W connection |
|---|---|
| VCC / VIN | 3V3(OUT), physical pin 36, via the shared 3.3 V rail/splitter |
| GND | Any Pico GND pin |
| SDA | GP16, physical pin 21 |
| SCL | GP17, physical pin 22 |

If the AHT10 comes on a Grove I2C cable, use the same Grove colour mapping as
the TSL2561: red=VCC, black=GND, white=SDA, yellow=SCL. The AHT10 I2C address
is `0x38`; it can coexist with the TSL2561 at `0x29`. [AHT10 Linux driver documentation](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/%2B/refs/heads/master/Documentation/hwmon/aht10.rst)

## Firmware status and flashing

The existing all-in-one firmware is:

- Source: `project_v3/multi_sensor_monitor.c`
- Firmware: `build/multi_sensor_monitor.uf2`

It currently reads the TSL2561, HC-SR04, and TCRT5000. It does **not yet read
the AHT10**; add an AHT10 driver before expecting temperature or humidity data
in Serial Monitor.

A Pico runs only one firmware image at a time. Copying a new UF2 replaces the
currently running one. A combined UF2 is the right approach when all sensors
are to be used together.

## Serial Monitor output expected from the current firmware

```text
light: 123.4 lux  distance: 42.7 cm  line: DETECTED
```

If a sensor is unplugged, the current test firmware reports its absence or a
timeout while continuing to read the other connected sensors.
