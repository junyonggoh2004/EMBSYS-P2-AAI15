# Pico-v3: Pico W Distributed IoT Edge Node

**Pico-v3** is a modular, configuration-driven sensor node framework for the Raspberry Pi Pico W. It allows for dynamic runtime configuration of sensors (UART, I2C, GPIO), local data processing using a Reverse Polish Notation (RPN) rule engine, and real-time telemetry via MQTT.

---

## 🏗️ Architecture & Design

The project is designed with a layered architecture to separate hardware abstraction, scheduling logic, and data distribution.

### System Data Flow

```mermaid
graph TD
    subgraph Hardware
        S_UART[UART Sensors]
        S_I2C[I2C Sensors]
        S_GPIO[GPIO Sensors]
    end

    subgraph "Bus Layer (Drivers)"
        Bus_UART[bus/uart.c]
        Bus_I2C[bus/i2c.c]
        Bus_GPIO[bus/gpio.c]
    end

    subgraph "Application Layer"
        Sched[Scheduler (scheduler.c)]
        Output[Output Hub (output_format.c)]
    end

    subgraph "Logic & Comms"
        Rules[Rule Engine (rules.c)]
        MQTT[MQTT Telemetry (mqtt_telemetry.c)]
        Console[Serial REPL]
    end

    %% Connections
    S_UART --> Bus_UART
    S_I2C --> Bus_I2C
    S_GPIO --> Bus_GPIO

    Bus_UART -->|Raw Bytes| Sched
    Bus_I2C -->|Raw Bytes| Sched
    Bus_GPIO -->|Raw Bytes| Sched

    Sched -->|Data Frame| Output

    Output -->|1. Print| Console
    Output -->|2. Evaluate| Rules
    Output -->|3. Publish| MQTT

    Rules -->|Trigger| Actions[Actions: GPIO / Log]
```

### Layer Descriptions

The firmware is structured into four distinct layers, allowing for modularity and dynamic runtime reconfiguration without recompilation.

1.  **Hardware Abstraction Bus Layer (`bus/`)**:
    *   **Hardware Abstraction**: Drivers for UART, I2C, and GPIO.
    *   **Standard Interface**: All drivers implement a common `bus_t` interface (`init`, `poll`, `stream`).
    *   **Responsibility**: Reads raw bytes from hardware and passes them to the scheduler.

2.  **Application Layer (`scheduler/`)**:
    *   **Scheduler (`scheduler.c`)**: Manages the polling frequency of all active sensors.
    *   **Config Parser (`config_parser.c`)**: Parses text-based configuration blocks.
    *   **Output Format (`output_format.c`)**: The central hub that distributes sensor data to the Console, Rule Engine, and MQTT.

3.  **Rule Engine (`rule_engine/`)**:
    *   **Logic Core**: Evaluates user-defined logic on incoming sensor data.
    *   **RPN Evaluator**: Uses a stack-based calculator for efficient expression parsing.
    *   **Actions**: Triggers physical outputs (GPIO) or alerts based on logic.

4.  **Communication (`mqtt/`)**:
    *   **Telemetry**: Publishes sensor data to the cloud/broker.
    *   **Remote Control**: Listens for configuration commands (Text or JSON) over MQTT.

---

## ⚙️ Configuration Guide

You can configure the Pico via **USB Serial (REPL)** or **MQTT**.

### 1. Line-Based Configuration (REPL & MQTT)
Used for defining sensors. Send this block to the serial terminal or the MQTT config topic.
Terminal should be set to CRLF

**REPL Config Structure (TERMINAL)**
```ini
BEGINCFG|name=GY511_ACC|proto=i2c|mode=poll|freq_hz=5|i2c.sda=0|i2c.scl=1|i2c.addr=0x1E|i2c.pre=0x00 0x10 0x02 0x00|i2c.post_delay_ms=2|i2c.reg=0x03|i2c.reg_size=1|i2c.read_len=6|i2c.restart=1|ENDCFG
```
`-m` is for inline

**MQTT In-line Config Structure**
```
mosquitto_pub -h <IP_ADDRESS> -t "pico/<Node_Number>/config" -m "BEGINCFG|name=GY511_ACC|proto=i2c|mode=poll|freq_hz=5|i2c.sda=0|i2c.scl=1|i2c.addr=0x1E|i2c.pre=0x00 0x10 0x02 0x00|i2c.post_delay_ms=2|i2c.reg=0x03|i2c.reg_size=1|i2c.read_len=6|i2c.restart=1|ENDCFG"
```

#### Protocol Specific Settings

| Protocol | Key | Description | Example |
| :--- | :--- | :--- | :--- |
| **Common** | `name` | Unique ID for the sensor | `name=temp_sensor` |
| | `proto` | Bus protocol Used | `proto=uart` |
| | `mode` | Polling or Streaming | `freq_hz=10` |
| | `freq_hz` | Polling frequency | `mode=poll` |
| **UART** | `uart.tx`, `uart.rx` | GPIO TX/RX pins | `uart.tx=0` |
| | `uart.baud` | Baud rate | `uart.baud=115200` |
| | `uart.bits` | Number of sensor bits | `uart.bits=8` |
| | `uart.parity` | Parity bits | `uart.parity=N` |
| | `uart.stop` | Number of stop bit | `uart.stop=1` |
| | `uart.line_mode` | 0=Raw byte mode, 1=wait for /n | `uart.line_mode=0` |
| | `uart.read_len` | Read per cycle | `uart.read_len=128` |
| *Optional UART* | `uart .pre` | Pre byte sent to wake sensor | `uart.pre=0xA5, 0x01` |
| | `uart.post_delay_ms` | Sleep time after sending pre bytes | `uart.post_delay_ms=50` |
| **I2C** | `i2c.sda`, `i2c.scl` | GPIO pins | `i2c.sda=4` |
| | `i2c.baud` | Baud rate | `i2c.baud=400000` |
| | `i2c.addr` | 7-bit Device Address | `i2c.addr=0x76` |
| | `i2c.reg` | Register to read | `i2c.reg=0xFA` |
| | `i2c.reg_size` | Size of register | `i2c.reg_size=1` |
| | `i2c.read_len` | Bytes to read | `i2c.read_len=6` |
| | `i2c.restart` | 1=Repeated Start, 0=Stop then Start | `i2c.restart=1` |
| *Optional I2C* | `i2c .pre` | Pre byte sent to wake sensor | `i2c.pre=0xA5, 0x01` |
| | `i2c.post_delay_ms` | Sleep time after sending pre bytes | `i2c.post_delay_ms=50` |
| **GPIO (Digital)** | `gpio.pin` | Pin to read | `gpio.pin=6` |
| | `gpio.pull` | Input pull: `up`, `down`, or `off` | `gpio.pull=off` |
| | `gpio.mode` | digital/pulse/onewire/counter | `gpio.mode=digital` |
| | `gpio.invert` | Logic inversion: 1=True, 0=False | `gpio.invert=0` |
| | `gpio.debounce_ms` | Debouncing time | `gpio.debounce_ms=100` |
| **GPIO (Pulse)** | `gpio.trig` | Output pin connected to sensor trig pin | `gpio.trig=16` |
| | `gpio.echo` | Input pin connected to Echo | `gpio.trig=17` |
| | `gpio.trig_us` | How long to hold trigger pin to initiate reading (microseconds) | `gpio.trig_us=10` |
| | `gpio.pulse_timeout_us` | Timeout (microseconds) | `gpio.pulse_timeout_us=25000` |
| | `gpio.pulse_guard_ms` | Minimum gap before another ultrasonic trigger | `gpio.pulse_guard_ms=60` |

### Maker Pi Pico people-flow setup

This project is built for a Raspberry Pi Pico 2 W mounted on a Cytron Maker Pi
Pico carrier. The current configuration profile reserves carrier GPIOs that are
connected to onboard hardware and validates every GPIO sensor before it is
initialised.

| Component | Maker Pi Pico connection | Firmware configuration |
| :--- | :--- | :--- |
| HC-SR04 A | Grove 2: GP2 TRIG, GP3 ECHO | `gpio.mode=pulse`, `gpio.trig=2`, `gpio.echo=3` |
| HC-SR04 B | Grove 3: GP4 TRIG, GP5 ECHO | `gpio.mode=pulse`, `gpio.trig=4`, `gpio.echo=5` |
| IR obstacle | GP6 | `gpio.mode=digital`, `gpio.pin=6` |
| IR line | GP7 | `gpio.mode=digital`, `gpio.pin=7` |
| DHT / AM2302 | Grove 5: GP8 DATA | `gpio.mode=onewire`, `gpio.pin=8` |

GPIO10–15 (microSD), GPIO16–17 (ESP-01 socket), GPIO18–19 (audio), GPIO20–22
(buttons), and GPIO28 (RGB LED) are unavailable to sensor configurations.
The scheduler also rejects a GPIO that has already been assigned to another
active GPIO sensor.

Each HC-SR04 needs a separate 5 V to 3.3 V level shifter or voltage divider on
its ECHO line before it reaches GP3 or GP5. Do not connect the ECHO output
directly to the Pico. Supply the HC-SR04 from 5 V and share its ground with the
Pico. The IR and DHT sensors use 3.3 V logic.

Grove Port 4 exposes both GP6 and GP7. Connect the two IR sensor outputs
separately: a normal Grove splitter connects the outputs together and cannot be
used for this arrangement.

#### Test each ultrasonic sensor first

Connect and verify one HC-SR04 before configuring the rest of the hardware.
Send the following through the USB serial REPL, then send `RUN` on a separate
line:

```ini
BEGINCFG|name=ULTRA_A|proto=gpio|mode=poll|freq_hz=1|gpio.mode=pulse|gpio.trig=2|gpio.echo=3|gpio.trig_us=10|gpio.pulse_timeout_us=30000|ENDCFG
```

A valid sample is printed as three bytes: pulse width in microseconds as a
little-endian 16-bit integer followed by `01`. Convert it with
`distance_cm = pulse_us / 58`. A single `00` byte means the ECHO pulse timed
out. Use `STOP` and `CLEAR` before changing a sensor configuration.

The complete current configuration is provided in
[`test_json/maker_pi_pico_people_flow.json`](test_json/maker_pi_pico_people_flow.json).
Publish it to the Pico after the individual tests:

```bash
mosquitto_pub -h <BROKER_IP> -t pico/pico-001/config \
  -f project_v3/test_json/maker_pi_pico_people_flow.json
```

The scheduler permits only one ultrasonic trigger every 60 ms across all pulse
sensors. Increase `gpio.pulse_guard_ms` when the sensor placement needs a
larger separation. The main loop runs every 10 ms, allowing the IR inputs to
poll at their configured 5 Hz rate.

### 2. JSON File-Based Configuration (MQTT Only)
You can send a JSON payload to `pico/<node_id>/config -f <filepath_of_json>` to configure sensors programmatically.
`-f` is for filepath

**Example JSON:**
```json
{
    "configs": [
        {
        "name": "LM75_TEMP",
        "proto": "i2c",
        "freq_hz": "1",
        "i2c": {
        "sda": "4",
        "scl": "5",
        "addr": "0x48",
        "reg": "0x00",
        "read_len": "2"
        }}
    ]
}
```

---

## 🧠 Rule Engine Guide

The Rule Engine allows the Pico to process data locally. Rules are defined using **Reverse Polish Notation (RPN)**.

### REPL Rule Structure (Terminal)
```ini
BEGINRULE|name=GY511_MOTION|source=GY511_ACC|calc=mx=s16be(0)|calc=mz=s16be(2)|calc=my=s16be(4)|calc=dx=mx-mx_prev|calc=dy=my-my_prev|calc=dz=mz-mz_prev|calc=motion=sqrt(dx*dx+dy*dy+dz*dz)|when=motion>120|action=log:motion detected|ENDRULE
```
### MQTT Inline Rule Structure
```ini
mosquitto_pub -h <IP_ADDRESS> -t "pico/<Node_Number>/config" -m "BEGINRULE|name=GY511_MOTION|source=GY511_ACC|calc=mx=s16be(0)|calc=mz=s16be(2)|calc=my=s16be(4)|calc=dx=mx-mx_prev|calc=dy=my-my_prev|calc=dz=mz-mz_prev|calc=motion=sqrt(dx*dx+dy*dy+dz*dz)|when=motion>120|action=log:motion detected|ENDRULE"
```

### JSON Rule Structure (MQTT Only)

```json
{
    "rules": [
        {
        "name": "HOT_ALARM",
        "source": "LM75_TEMP",
        "calc": [
            "raw=s16be(0)",
            "celsius=raw/256"
        ],
        "when": "celsius > 30",
        "action": "log:Temp is high: $value"
        }
    ]
}
```

### 1. `calc`: Extracting Data
Defines variables by extracting bytes from the raw sensor frame.
*   **Byte Extraction:**
    *   `u8(i)/s8(i)`: Unsigned/Signed 8-bit integer at index `i`.
    *   `s16le(i)/u16le(i)`: 16-bit (Little Endian) at index `i`.
    *   `u16be(i)/s16be(i)`: 16-bit (Big Endian) at index `i`.

**Example:** `calc=temp=s16le(4)/100.0` (Reads 2 bytes at index 4, divides by 100).

### 2. `when`: Logic Conditions (RPN)
A condition that must be true (non-zero) for the action to fire.
*   **Functions:**
    *   `abs(x)`: Absolute Value.
    *   `sqrt(x): Square Root.
    *   `mag3(x): 3D magnitude (sqrt{x^2+y^2+z^2}).
*   **Math:** `+`, `-`, `*`, `/`, `%`
*   **Logical Operators:** `+`, `-`, `*`, `/`, `>`, `<`, `==`, `&&` (AND), `||` (OR).
*   **Previous Values:** Append `_prev` to a variable name to access its value from the *previous* sample.

**Example:** `x-x_prev` (Used to calculate rate of change).

### 3. `action`: Triggers
What to do when the condition is met.
*   **`log:<message>`**: Prints a message to the console/MQTT. $value is replaced by the rule result.
*   **`gpio:<pin>=HIGH`**: HIGH,Sets GPIO pin high.
*   **`gpio:<pin>=LOW`**: LOW,Sets GPIO pin low.
*   **`gpio:<pin>=TOGGLE`**: Toggles the pin state.
*   **`gpio:<pin>=PULSE`**:<ms>: Pulses the pin HIGH for ms milliseconds.
*   **`pwm:<pin>=<val>`**: Sets PWM duty. 0.5 (50%) or 75 (75%). pwm:<pin>=OFF disables it.
*   **` cmd.to:<node>:<CMD>`**: Sends a command to a remote node via MQTT.
*   **`cfg.to:<node>:<BLK>`**: Sends a Configuration block to a remote node.
---

## 🎮 Usage Guide

### Serial Terminal (REPL) Commands

*   **`RUN`**: Start the scheduler (begin polling sensors).
*   **`STOP`**: Stop the scheduler.
*   **`CLEAR`**: Remove all configured sensors and rules.
*   **`SHOW`**: Display current configuration and active rules.
*   **`BEGINCFG ... ENDCFG`**: Enter configuration mode.
*   **`BEGINRULE ... ENDRULE`**: Enter rule definition mode.

### MQTT Topics & JSON Injections
**Config**: `pico/<node_id>/config`: Send JSON or Text configuration blocks.
**Commands**: `pico/<node_id>/cmd`: Send text commands (`RUN`, `STOP`, `CLEAR`, `SHOW`).
**Telemetry**: `pico/<node_id>/sensor/<sensor_name>`: JSON data
**Human Readable** `pico/<node_id>/sensor/<sensor_name>/line`: ASCII TEXT

**Example Complete JSON with Config + Rule:**
```json
{
  "configs": [
    {
      "name": "LM75_TEMP",
      "proto": "i2c",
      "freq_hz": "1",
      "i2c": {
        "sda": "4",
        "scl": "5",
        "addr": "0x48",
        "reg": "0x00",
        "read_len": "2"
      }
    }
  ],
  "rules": [
    {
      "name": "HOT_ALARM",
      "source": "LM75_TEMP",
      "calc": [
        "raw=s16be(0)",
        "celsius=raw/256"
      ],
      "when": "celsius > 30",
      "action": "log:Temp is high: $value"
    }
  ]
}
```


### Sensor Bridging

*Sensor Bridging allows Node A to "listen" to a sensor physically connected to Node B and treat it as if it were its own local sensor. This allows for distributed logic.*

---
**1. Control Flow (Pico 001 --> Pico 002)**

**Scenario**: Pico 001 detects a local event and activates a sensor on Pico 002

```
sequenceDiagram
    participant P1_Rule as Pico 001 Rule Engine
    participant P1_MQTT as Pico 001 MQTT Client
    participant Broker as MQTT Broker
    participant P2_MQTT as Pico 002 MQTT Client
    participant P2_Parser as Pico 002 Config Parser
    participant P2_Sched as Pico 002 Scheduler

    Note over P1_Rule: 1. Rule Trigger
    P1_Rule->>P1_Rule: Condition Met (e.g. Button Press)
    P1_Rule->>P1_MQTT: Action: cfg.to:pico-002:BEGINCFG...
    
    Note over Broker: 2. Transport
    P1_MQTT->>Broker: Publish "pico/pico-002/config"
    Broker->>P2_MQTT: Deliver Payload
    
    Note over P2_Parser: 3. Execution
    P2_MQTT->>P2_Parser: Receive "BEGINCFG..."
    P2_Parser->>P2_Sched: Add New Sensor (e.g. LED/Alarm)
    P2_Sched->>P2_Sched: Start Polling/Streaming
```
---
**Command Format (RUN, STOP, CLEAR)**

Use this to control the state of the scheduler on a remote node.

**A. Inside a Rule (Rule Action)**: Use the `cmd.to` action type.

- **Format**: `action=cmd.to:<node_id>:<COMMAND>`

- **Example**: `action=cmd.to:pico-002:RUN`


**B. Via MQTT**: Publish the command string directly to the cmd topic.

- **Topic**: `pico/<node_id>/cmd`

- **Payload**: `RUN, STOP, CLEAR, or SHOW `

- **Example**: `mosquitto_pub -t "pico/pico-002/cmd" -m "RUN"`

---
**Configuration Injection Format**

*Use this to send a sensor configuration to a remote node (e.g., to turn on a sensor so you can bridge it).*

**A. Inside a Rule (Rule Action)**: Use the `cfg.to` action type.

- **Format**: `action=cfg.to:<node_id>:<FULL_CONFIG_BLOCK>`

- **Example**: `action=cfg.to:pico-002:BEGINCFG|name=LED|proto=gpio|gpio.mode=digital|gpio.pin=25|ENDCFG`
    
**B. Via MQTT:** Publish the config block to the config topic.

- **Topic**: `pico/<node_id>/config`

- **Payload**: `BEGINCFG|...|ENDCFG` (or JSON)

- **Example**: `mosquitto_pub -t "pico/pico-002/config" -m "BEGINCFG|name=TEMP|proto=i2c|...|ENDCFG"`

---
**2. Bridging Data Flow (Pico 002 --> Pico 001)**

**Scenario**: Pico 002 is now running its sensor. Pico 001 subscribes to that data to make decisions.
```
sequenceDiagram
    participant P2_HW as Pico 002 Hardware
    participant P2_MQTT as Pico 002 MQTT Client
    participant Broker as MQTT Broker
    participant P1_MQTT as Pico 001 MQTT Client
    participant P1_Bridge as Pico 001 Sensor Bridge
    participant P1_Rule as Pico 001 Rule Engine

    Note over P2_HW: 1. Physical Sensing
    P2_HW->>P2_MQTT: Read Sensor & Publish Line
    P2_MQTT->>Broker: Publish "pico/pico-002/sensor/NAME/line"

    Note over P1_MQTT: 2. Bridge Subscription
    Broker->>P1_MQTT: Receive Payload (e.g. "001 CE")
    P1_MQTT->>P1_Bridge: Callback: on_remote_line()

    Note over P1_Bridge: 3. Injection
    P1_Bridge->>P1_Bridge: Map "pico-002/NAME" -> "VIRTUAL_SENS"
    P1_Bridge->>P1_Rule: Inject Raw Bytes as "VIRTUAL_SENS"

    Note over P1_Rule: 4. Logic
    P1_Rule->>P1_Rule: Calc & Action
```
---
**Bridge Setup Command**

*To actually bridge (listen to) the remote sensor you just activated, you must send a configuration to the Receiver Node.*

- **Topic**: `pico/<receiver_node_id>/config`

- **Payload**: `BEGINCFG|name=<LOCAL_ALIAS>|proto=mqtt|remote_node=<REMOTE_ID>|remote_source=<REMOTE_SENSOR_NAME>|ENDCFG`
---
