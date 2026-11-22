# Arduino/FreeRTOS baseline: set up and test

These are the steps for the earlier Arduino-ESP32/FreeRTOS build (`esp32s3_edge/`, `tools/s3.sh`), kept as the measured baseline (task 21, [PARITY.md §3](parity/PARITY.md#3-esp32-s3-results-tasks-13-to-21)), with Windows commands. The board pinout is in [section 8](README.md#board-pinout). Going from the µT-Kernel build back to this one, erase the flash first ([SETUP.md](SETUP.md#testing-on-µt-kernel-tasks-2732)).


---

## Part A: Install

Once per laptop, about 15 minutes.

**Step 1. Install the tools.**

| Mac (Terminal: Cmd+Space, "Terminal") | Windows (PowerShell: Start, "PowerShell") |
|---|---|
| `xcode-select --install` | `winget install Git.Git` |
| Homebrew (skip if `brew -v` works): `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"` | `winget install GitHub.cli` |
| `brew install arduino-cli mosquitto gh` | `winget install Python.Python.3.12` |
| | `winget install ArduinoSA.CLI` |
| | `winget install EclipseFoundation.Mosquitto` |
| | Close PowerShell. From now on use **Git Bash** (Start, "Git Bash") |

From Step 2 on, the commands are the same on both (Mac Terminal or Windows Git Bash).

**Step 2. Download the project.**
```sh
gh auth login          # GitHub.com, HTTPS, log in with the browser
cd ~ && gh repo clone junyonggoh2004/EMBSYS-P2-AAI15
cd ~/EMBSYS-P2-AAI15 && git checkout main
```
Already have it? `cd` into it and run `git pull`.

**Step 3. Install the board support, MQTT library and test tools.**
```sh
sh tools/s3.sh setup
```
You should see: `Done. Edit esp32s3_edge/wifi_credentials.h ...`

**Step 4. Check the laptop.**
```sh
sh tools/s3.sh check
```
You should see: `esp32 core: 3.3.12`, `MQTT lib: 2.5.3` and `Global variables use 21592 bytes`.


---

## Part B: Wi-Fi and broker

**Step 5. Find the laptop's IP.** Mac: `ipconfig getifaddr en0`. Windows: `ipconfig`, then **IPv4 Address** under "Wireless LAN adapter Wi-Fi".

**Step 6. Give the board your Wi-Fi.** Mac: `open -e esp32s3_edge/wifi_credentials.h`. Windows: `notepad esp32s3_edge/wifi_credentials.h`. Set `WIFI_SSID` (2.4 GHz network only), `WIFI_PASS`, and `MQTT_HOST` (the IP from Step 5). Save. This file is never committed.

**Step 7. Start the broker in a second window and leave it open.**

| Mac (Cmd+N) | Windows (another Git Bash) |
|---|---|
| `printf "listener 1883 0.0.0.0\nallow_anonymous true\n" > ~/mosq.conf` | same |
| `mosquitto -c ~/mosq.conf -v` | `"/c/Program Files/mosquitto/mosquitto.exe" -c ~/mosq.conf -v` |

The broker is the MQTT message hub: the S3 sends its sensor data and heartbeats to it, and the MQTT tests reach the S3 through it. Keep it open until you finish Part D (Steps 19–21 and 24 need it; the other tests run over USB), then close it with Ctrl+C. Click **Allow** if asked about incoming connections. "Address already in use" means a broker is already running: Mac `pkill mosquitto`; Windows `net stop mosquitto` in an Administrator PowerShell; then run it again.


---

## Part C: Wire and flash

**Step 8. Two test wires on the S3:** **GP1 ↔ GP2** and **GP5 ↔ GP6**.

**Step 9. Plug the S3 in** with a USB-C data cable. On Windows it appears as a COM port (Device Manager, Ports).

**Step 10. Flash.**
```sh
sh tools/s3.sh flash
```
You should see: `Hash of data verified.` If it says "serial TX path seems to be down", use another cable and try again.

**Step 11. Watch it live.**
```sh
sh tools/s3.sh monitor
```
Within about 20 s: `[WIFI] ip=...` and `[MQTT] Subscribed OK to: broadcast`, and the broker window shows a heartbeat from `s3-001` every 10 s. Type `HELP` and press Enter for the command list; **Ctrl+C** leaves. If you see `Guru Meditation Error ... Interrupt wdt timeout on CPU1` and then `Port closed`, that is the known start-up defect D-15 (mostly the first boot after flashing): the board restarts itself, so run the monitor again.


---

## Part D: Tests

Close the monitor first (Ctrl+C): a test and the monitor cannot use the USB port at the same time.

| Step | Command | You should see | What it does | Why it matters |
|---|---|---|---|---|
| 12 | `sh tools/s3.sh test 4a` | `14/14 passed` | HELP, SHOW, RUN, STOP, a sensor, rules and the AI model | Same commands and replies as the Pico |
| 13 | `sh tools/s3.sh test 4b` | `25/25 passed` | Switches GP5 and reads it on GP6, counts pulses, PULSE and PWM | Digital in/out, counting and output actions work |
| 14 | `sh tools/s3.sh test 4c-uart` | `8/8 passed` | Sends bytes out of GP1 and reads them on GP2 | Serial (UART) sensors can be read |
| 15 | `sh tools/s3.sh test 5-neg` | `27/27 passed` | Wrong protocols, missing pins, broken rules, too many sensors | Bad input is refused, never crashes the board |
| 16 | `sh tools/s3.sh test t02` / `t03` / `t04` / `t08` / `t09` | 7/7, 6/6, 13/13, 26/26, 5/5 | Inverted input; pin 99; 8 fast sensors; UART settings; UART non-stop | Settings work, impossible pins are refused, no data lost under load |
| 17 | `sh tools/s3.sh test t06` | `6/7 passed` | Pulse and one-wire sensors with nothing connected | Missing sensors time out; the one fail is D-07, kept to match the Pico |
| 18 | Unplug and replug USB, wait 10 s, then `sh tools/s3.sh test t10` | `7/7 passed` | One rule that should fire, one that must not | Rules act only when true (the replug clears GP5) |
| 19 | `sh tools/s3.sh mqtt <your-ip>` | `27/27 passed` | Configs, commands, data and an AI model upload over Wi-Fi | The wireless side works like the Pico; bridging (broken on the Pico, D-10) works |
| 20 | `sh tools/s3.sh model <your-ip>` | `9/9 passed` | Good, corrupt, too-big and cut-off model uploads, then a reboot | Bad uploads never break the AI; the good model survives power-off |
| 21 | `sh tools/s3.sh faults <your-ip>` | every line `PASS` | Garbage and huge messages, a Wi-Fi drop | The node recovers by itself and keeps sampling offline |
| 22 | `sh tools/s3.sh timing` | `PASS` rows (4 min) | Sample spacing for 6 sensors | On time within 5% (the Pico was stuck at 1 per second, D-06) |
| 23 | `sh tools/s3.sh compare 4a` | Pico and S3 text | The Pico's and the S3's log of the same test | See that the output is identical |
| 24 | `sh tools/s3.sh endurance <your-ip>` (optional) | `6/6 passed` (30 min) | Sensors, rules, AI and MQTT for 30 minutes | No crash, gaps or memory leak over time |


---

## Part E: Real sensors

Use one sensor at a time. No breadboard and no resistor are needed for these two (see "Why no resistor" in section 8).

### Step 25. GY-511 compass and accelerometer (I2C)

1. Unplug the USB cable.
2. Remove the **GP1 ↔ GP2** test wire.
3. Connect 4 wires (leave the GY-511's other pins empty):

   | GY-511 pin | S3 pin | Where on the S3 |
   |---|---|---|
   | VIN | 3V3 | right side, 3rd from the top |
   | GND | GND | right side, 2nd from the top |
   | SDA | GP1 | left side, 3rd from the top |
   | SCL | GP2 | left side, 4th from the top |

4. Plug the USB cable back in.
5. Run:
   ```sh
   sh tools/s3.sh test 4c-mag
   ```

**You should see:** `15/15 passed`.
**Why it proves the sensor is real:** the chip answers with its factory ID `48 34 33` ("H43"), which no wire can fake, and the accelerometer measures gravity (about 1 g).

### Step 26. Real digital input (MH light sensor or the 3-pin IR board)

1. Unplug the USB cable and remove the GY-511.
2. Connect 3 wires (the MH board's 4th pin, **AO**, is an analog output and stays empty: D1 reads digital 0/1):

   | Sensor pin | S3 pin | Where on the S3 |
   |---|---|---|
   | VCC (or +) | 3V3 | right side, 3rd from the top |
   | GND (or −) | GND | right side, 2nd from the top |
   | DO (MH board) or OUT/S (IR board) | GP1 | left side, 3rd from the top |

3. Plug the USB cable back in.
4. **See it live first** (in your main window; the broker window is not needed):
   1. `sh tools/s3.sh monitor` and wait for `[MQTT] Subscribed OK to: broadcast`.
   2. Paste `BEGINCFG|name=LIGHT|proto=gpio|gpio.mode=digital|gpio.pin=1|gpio.pull=up|mode=poll|freq_hz=2|ENDCFG` and press Enter. The board replies `CFG: ok (added #0: LIGHT)`.
   3. Type `RUN`. Watch the lines `proto=gpio src=LIGHT len=3 ts=... : 00 ...`; the first number after the colon is the reading (`00` or `01`). Ignore the `[SCHED]` lines.
   4. In normal light, slowly turn the blue knob until the sensor's right-hand LED just switches; the reading flips `00` ↔ `01` with it. Then cover it with your hand and shine a phone torch on it: the LED and the reading flip each time. (LED flips but the reading never does: the DO wire is not on GP1. LED never flips: check VCC and GND.)
   5. Type `STOP`, then `CLEAR`, then Ctrl+C to close the monitor (a test cannot run while the monitor is open).
5. Run the test:
   ```sh
   sh tools/s3.sh test 4d-ir
   ```
6. When `PASS  cfg-ircnt` appears, the screen goes quiet for **40 seconds**: in that time cover and uncover (or torch) the sensor 5–6 times. For the IR board, wave your hand over it.

**You should see:** `7/7 passed`.
**Why it proves the input works:** a real sensor drives the pin to both 0 and 1, the inverted copy reads the opposite, and the counter counts the changes.

### Why no resistor

An S3 pin can take at most 3.3 V. The GY-511, MH board and IR board are powered from the S3's **3V3** pin, so their signals can never be higher than 3.3 V, and the pull-up or pull-down a digital input needs is built into the chip (`gpio.pull=up` or `down`). The test wires join an S3 pin to another S3 pin, also 3.3 V. Only the **HC-SR04** needs a resistor divider: it is powered from **5V**, so its Echo signal is 5 V and must be divided down before it reaches an S3 pin (wiring in [SETUP.md](SETUP.md#pin-power-and-wiring-task-8)).

If every step matched, this laptop has built, flashed and verified the Arduino baseline by itself.
