# Pico Reference and ESP32-S3 Arduino Setup

Sections 0–13 are the complete guide for getting the existing Pico reference firmware running on your own hardware, from an empty machine to a board that is confirmed working. They cover what to install, wire, build, flash and verify. The separate ESP32-S3 Arduino setup plan follows section 13. Since Tribunal 1 the D1 firmware on the ESP32-S3 runs on µT-Kernel 3 SMP: its setup is the last section, [ESP32-S3 on µT-Kernel 3 SMP](#esp32-s3-on-µt-kernel-3-smp-tasks-2534). The ESP32-S3 Arduino sections remain for the FreeRTOS baseline build and the D2 SPI work.

The Pico procedure was verified end to end on macOS (Apple Silicon) on 2026-09-18. Windows and Linux notes are marked wherever a step differs. The minimal ESP32-S3 Arduino sketch was verified on one physical board on 2026-10-13; broader board integration remains pending.

## 0. What you are building, and on what hardware

`project_v3` is the FlexiEdgePico firmware: a configuration-driven sensor node that reads UART, I2C, or GPIO sensors, evaluates rules on the data locally, and can publish over MQTT. Full behaviour and configuration syntax is documented in `project_v3/README.md`; this document only covers getting it built and onto a board. The D1 porting checklist is in the top-level `README.md`.

The board target is fixed in `CMakeLists.txt` (`PICO_BOARD=pico2_w`): this firmware is built for the **Raspberry Pi Pico 2 W**, which uses the RP2350 chip. It will not run on the original Pico W (RP2040) — the two are different microcontrollers and a binary built for one will not boot on the other. If your team's materials list has plain "Pico W" and not "Pico 2 W", confirm which one you actually have before going further.

## 1. Hardware you need for a basic bring-up

At minimum: one Raspberry Pi Pico 2 W, and a USB cable that carries data (some phone-charger-only cables don't; if the board never enumerates as a USB device, try a different cable first).

To test an actual sensor rather than just confirming the board boots, you additionally need one of the sensors the team has already collected: the HC-SR04 ultrasonic sensor, or the small I2C breakout (GY-511 candidate). Wiring for both is given in section 5.

## 2. Pico 2 W pinout reference

The pin numbers below are what the configuration keys in `project_v3/README.md` (`i2c.sda`, `gpio.trig`, `uart.tx`, and so on) refer to — they are GPIO numbers, not the physical pin position on the board edge. This table is the general-purpose pin layout, which is identical between the Pico 2 and the Pico 2 W since both use the same RP2350 chip; the difference between them is the wireless chip and antenna, and the onboard LED, which is not wired to a plain GPIO on the "W" variant. Ignore the LED pin for this project.

| GPIO | Common alt functions | GPIO | Common alt functions |
|---|---|---|---|
| GP0 | UART0 TX | GP16 | SPI0 RX, I2C0 SDA, UART0 TX |
| GP1 | UART0 RX | GP17 | SPI0 CSn, I2C0 SCL, UART0 RX |
| GP2 | I2C1 SDA, SPI0 SCK | GP18 | SPI0 SCK, I2C1 SDA |
| GP3 | I2C1 SCL, SPI0 TX | GP19 | SPI0 TX, I2C1 SCL |
| GP4 | I2C0 SDA, SPI0 RX | GP20 | I2C0 SDA |
| GP5 | I2C0 SCL, SPI0 CSn | GP21 | I2C0 SCL |
| GP6 | I2C1 SDA, SPI0 SCK | GP22 | general purpose |
| GP7 | I2C1 SCL, SPI0 TX | GP26 | ADC0, I2C1 SDA |
| GP8 | UART1 TX, I2C0 SDA | GP27 | ADC1, I2C1 SCL |
| GP9 | UART1 RX, I2C0 SCL | GP28 | ADC2 |
| GP10 | I2C1 SDA, SPI1 SCK | | |
| GP11 | I2C1 SCL, SPI1 TX | | |
| GP12 | I2C0 SDA, SPI1 RX | | |
| GP13 | I2C0 SCL, SPI1 CSn | | |
| GP14 | I2C1 SDA, SPI1 SCK | | |
| GP15 | I2C1 SCL, SPI1 TX | | |

Power and ground pins used below: **3V3(OUT)** supplies 3.3 V for sensors, **GND** is ground (several are available around the board edge), **VBUS** carries the raw 5 V from USB.

## 3. Wire up a sensor

Do this now so you have something concrete to test against once the firmware is flashed. If you only want to confirm the board itself boots first, skip to section 6 and come back here afterwards.

### I2C sensor (GY-511 or similar breakout)

The existing test configs in `project_v3/test_json/gy511.json` and `bmp388.json` both assume I2C0 on GP4/GP5, so wire to match:

- Sensor VCC to Pico **3V3(OUT)**
- Sensor GND to Pico **GND**
- Sensor SDA to Pico **GP4**
- Sensor SCL to Pico **GP5**

Check the breakout's supply and logic voltage from its markings or datasheet before wiring. If it runs at 3.3 V it connects directly as shown; if it needs 5 V logic, its SDA and SCL lines need level shifting.

### HC-SR04 ultrasonic sensor

`project_v3/README.md` documents a GPIO pulse mode built specifically for this sensor, using `gpio.trig` and `gpio.echo`. A standard HC-SR04 needs a 5 V supply and drives its Echo pin at 5 V, but the Pico's GPIO pins are only rated for 3.3 V. Power the sensor from 5 V and bring Echo down to 3.3 V with a resistor divider, or a logic level shifter if you have one:

- Sensor VCC to Pico **VBUS** (5 V from USB, physical pin 40)
- Sensor GND to Pico **GND**
- Sensor Trig to Pico **GP16** (a 3.3 V trigger pulse is enough for the HC-SR04)
- Sensor Echo through the divider to Pico **GP17**: a 1 kΩ resistor from Echo to GP17, and a 2 kΩ resistor from GP17 to GND. This brings the 5 V echo down to about 3.3 V.

**Voltage warning:** do not connect Echo directly to a GPIO pin while the sensor is powered from 5 V, since repeated 5 V signals can damage the pin. Do not power a standard HC-SR04 from 3V3(OUT) either: most units are specified for 5 V and give unreliable readings or none at 3.3 V. Only use 3.3 V if your specific module's datasheet says it supports it.

### UART sensor module

Wire the module's TX to the Pico's RX and the module's RX to the Pico's TX (crossed, not straight through):

- Module TX to Pico **GP1** (UART0 RX)
- Module RX to Pico **GP0** (UART0 TX)
- Module VCC/GND per its datasheet — confirm it's 3.3 V logic before connecting it to the Pico directly

The exact baud rate and framing depend on which module this is; check its datasheet and set `uart.baud` accordingly when you configure it.

## 4. Install software prerequisites

You need:

- **Git**, to clone this repository and the Pico SDK.
- **CMake**, version 3.13 or later, which generates the build files.
- **Python 3**, used by the Pico SDK's build tooling.
- **GNU Make**. On macOS, Apple's bundled `make` is an old BSD version that doesn't behave correctly for this build; install a current one via Homebrew (`brew install make`) and make sure its `gnubin` directory comes before `/usr/bin` on your PATH.
- **The Arm GNU Toolchain, version 14.2.Rel1 specifically.** Not "whatever `arm-none-eabi-gcc` your package manager gives you" — see section 8 for exactly why this matters. Installed separately in section 8, not here, so do the rest of the setup first.

## 5. Get the Pico SDK

Clone the SDK and its submodules somewhere outside this repository, for example `~/pico/pico-sdk`:

```
git clone https://github.com/raspberrypi/pico-sdk.git ~/pico/pico-sdk
cd ~/pico/pico-sdk
git submodule update --init
```

You need SDK version 2.x or later — version 1.5.x has no definition for the `pico2_w` board and CMake will fail immediately. Check what you have with `git -C ~/pico/pico-sdk describe --tags`.

Point CMake at it by setting an environment variable. Add this to your shell profile (`~/.zshrc` or `~/.bashrc` on macOS/Linux; a permanent system environment variable on Windows) and open a new terminal:

```
export PICO_SDK_PATH=$HOME/pico/pico-sdk
```

## 6. Clone this repository and get the code

```
git clone <this repo's URL>
cd EMBSYS-P2-AAI15
git checkout s3-port
git submodule update --init
```

`s3-port` is the D1 branch. It starts from the supplied `Pico_Sample` code and adds the TFLite Micro submodule reference that was missing from it, plus this document. `git submodule update --init` downloads that library; if you skip it, the build fails with the missing-library error described in section 10.

## 7. Create your WiFi and MQTT credentials file

```
cp project_v3/include/wifi_credentials.h.example project_v3/include/wifi_credentials.h
```

Edit `project_v3/include/wifi_credentials.h` and fill in your WiFi SSID, password, and MQTT broker IP address. This file is listed in `.gitignore` — never commit it, since it's specific to your network. Placeholder values are enough to build and flash if you only want to confirm the board boots: the firmware logs a WiFi connection failure and carries on rather than halting (see `project_v3/main.c`), so a wrong SSID does not block the rest of this guide.

## 8. Get the pinned toolchain and understand why it's pinned

`CMakeLists.txt` specifies `toolchainVersion 14_2_Rel1`. This is a real requirement, not a suggestion: the inference engine links against a bundled snapshot of Google's flatbuffers headers (pulled in via the TFLite Micro submodule from section 6), and that snapshot fails to compile under GCC 15 with a template argument deduction error inside `flatbuffer_builder.h`. It compiles cleanly under GCC 14.2. This was confirmed by building the exact same source tree with both compilers on 2026-09-18.

Check what you have first:

```
arm-none-eabi-gcc --version
```

If it reports version 14.x, skip to section 9 and use it directly. If it reports 15.x (or you have nothing installed), get 14.2.Rel1 specifically. Install it alongside any existing toolchain rather than replacing it, in case you need a newer one for other coursework.

Download the archive matching your platform from Arm's developer site:

```
# macOS, Apple Silicon
curl -L -o toolchain.tar.xz "https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi.tar.xz"
```

For macOS Intel, Linux, or Windows, use the matching filename for your platform from the same Arm downloads page (search "Arm GNU Toolchain 14.2.Rel1 downloads"): `darwin-x86_64`, `x86_64-arm-none-eabi` for Linux, or the Windows `.exe` installer / `mingw-w64-i686-arm-none-eabi.zip`.

Extract it to a fixed location, for example `~/.arm-toolchains/`:

```
mkdir -p ~/.arm-toolchains
tar xf toolchain.tar.xz -C ~/.arm-toolchains
```

Deliberately do not add this toolchain's `bin` directory to your system PATH. It's referenced by full path in the next step instead, so it stays scoped to this project and won't shadow a toolchain you use elsewhere.

## 9. Configure the build

From the repository root:

```
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=$HOME/.arm-toolchains/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi/bin/arm-none-eabi-gcc \
  -DCMAKE_CXX_COMPILER=$HOME/.arm-toolchains/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi/bin/arm-none-eabi-g++ \
  -DCMAKE_ASM_COMPILER=$HOME/.arm-toolchains/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi/bin/arm-none-eabi-gcc \
  ..
```

Adjust the three paths to wherever you extracted the toolchain, and to your platform's executable suffix (`arm-none-eabi-gcc.exe` on Windows). If `arm-none-eabi-gcc --version` already reported 14.x systemwide in section 8, drop all three flags and just run `cmake -DCMAKE_BUILD_TYPE=Release ..`.

Expect this near the end of the output:

```
-- Configuring done
-- Generating done
-- Build files have been written to: .../build
```

The first run may also print a warning about picotool and download/build it from source — this is expected and only happens once per machine.

If configure fails with an error mentioning `PICO_BOARD` or an unrecognized board, your Pico SDK is too old (see section 5). If it fails mentioning `PICO_SDK_PATH not found` or similar, the environment variable from section 5 isn't set in the shell you're running in — open a new terminal after editing your shell profile.

## 10. Build

```
make -j$(sysctl -n hw.ncpu)
```

On Linux, use `make -j$(nproc)`. On Windows with the Ninja generator (`cmake -G Ninja ...`), run `ninja` instead of `make`.

A successful build ends with:

```
[100%] Linking CXX executable project_v3.elf
[100%] Built target project_v3
```

Confirm the flashable file exists:

```
ls -lh project_v3.uf2
```

If the build instead fails with errors inside `project_v3/third_party/pico-tflmicro/src/third_party/flatbuffers/...` mentioning `PushElement` or "no matching function," you're building with GCC 15 rather than the pinned 14.2 toolchain — go back to section 9 and make sure the `-DCMAKE_C_COMPILER` flags actually point at the 14.2 binaries, then delete the `build` directory and reconfigure from scratch (CMake caches the compiler choice on first configure and won't pick up a change otherwise).

If the build fails with `add_library` given no source files for `pico-tflmicro`, the submodule from section 6 didn't actually get populated — run `git submodule update --init` again and check that `project_v3/third_party/pico-tflmicro/src` is not empty.

## 11. Flash the board

Hold the BOOTSEL button on the Pico 2 W module itself (next to the USB connector, not any carrier board button) while plugging it into your computer, then release it. The board mounts as a USB mass storage drive (named something like `RP2350`). Drag `build/project_v3.uf2` onto that drive. The board unmounts and reboots on its own once the copy finishes — this is normal and means the flash succeeded.

If no drive appears: hold BOOTSEL, plug in, and don't release it until after the drive shows up in your file browser; timing is a bit fussy on some machines. If it still doesn't appear, try a different USB cable — many are power-only and carry no data.

## 12. Verify the board is actually running the firmware

This is the step the original setup notes skipped, and it's the one that actually proves everything above worked, not just that it compiled.

**Find the serial port.** On macOS/Linux:

```
ls /dev/tty.usbmodem* 2>/dev/null || ls /dev/ttyACM* 2>/dev/null
```

On Windows, open Device Manager and look under "Ports (COM & LPT)" for a new COM port after plugging the board in.

**Open a serial terminal to it.** The firmware does not echo what you type, so use a terminal that shows your own input. On macOS/Linux, pySerial's miniterm does this:

```
python3 -m serial.tools.miniterm /dev/tty.usbmodemXXXX 115200 --echo
```

If `pyserial` is missing, install it with `pip3 install pyserial`. On a Homebrew Python this is refused as an "externally-managed environment"; use `pip3 install --user --break-system-packages pyserial` instead. Exit miniterm with `Ctrl-]`. The baud rate is ignored by USB CDC serial but must be given. On Windows, use PuTTY, TeraTerm, or the Arduino IDE's Serial Monitor connected to the COM port found above, with local echo on.

Avoid `screen` here: it shows nothing as you type, and a `screen` session left detached in the background keeps the port open, so every later connection fails with "Resource busy". If that happens, `screen -ls` lists the stale session and `screen -X -S <name> quit` closes it.

Three rules for typing into the REPL:

1. Line endings must end in LF: CRLF or LF works, CR alone does not. miniterm sends CRLF by default.
2. **Never correct a typo with Backspace or the arrow keys.** The REPL has no line editing, so those keys are stored as invisible characters inside the command and it comes back as `Unknown:`. If you mistype, press Enter, ignore the `Unknown:` reply, and type the whole line again.
3. Paste long `BEGINCFG` lines in one go rather than typing them.

**Check 1 — the board boots and attempts WiFi.** Within a couple of seconds of connecting, you should see one of:

```
WiFi connected
```
or
```
WiFi connect failed
```

Either is fine for this check — it proves the firmware is actually running, not just that the board powered on. The boot messages print only once, so if your terminal connected afterwards you won't see them. Instead you will see the running firmware's heartbeat: a `[SCHED …] run_all: running=0` line every second, and with placeholder WiFi credentials a `[MQTT] Status: Disconnected, Broker: 0.0.0.0` line every few seconds. Both are normal and prove the firmware is running just as well. If you see nothing at all, unplug and replug the board and reconnect.

**Check 2 — the REPL responds to commands.** Type `SHOW` and press Enter. Expect:

```
[SHOW] sampling=0
```

Type `RUN` and press Enter. Expect:

```
RUN
```

Type `STOP` and press Enter. Expect:

```
STOP
```

If you get these exact responses, the REPL, scheduler, and USB serial stack are all confirmed working. This is the point where you can say "my setup works" for the purposes of the team's sprint task, even without a sensor wired up yet.

**Check 3 — a real sensor produces data.** With a sensor wired per section 3, send its configuration block over the same serial terminal. For the I2C sensor from section 3:

```
BEGINCFG|name=GY511_ACC|proto=i2c|mode=poll|freq_hz=5|i2c.sda=4|i2c.scl=5|i2c.addr=0x1E|i2c.pre=0x00 0x10 0x02 0x00|i2c.post_delay_ms=2|i2c.reg=0x03|i2c.reg_size=1|i2c.read_len=6|i2c.restart=1|ENDCFG
```

Expect, in this order (the timestamps will differ):

```
OK: Config applied (inline, delim='|')
[SCHED    962940ms] add[0]: GY511_ACC on i2c freq=5.000Hz mode=poll I2C(sda=4,scl=5,addr=0x1E,reg=0x3,len=6)
[SCHED] added: GY511_ACC on i2c
CFG: ok (added #0: GY511_ACC)
```

Then type `RUN`. Within a second you should start seeing lines like:

```
proto=i2c src=GY511_ACC len=6 ts=12345 : 00 12 FF EA 00 33
```

For the HC-SR04 from section 3, use a pulse-mode config instead:

```
BEGINCFG|name=HCSR04|proto=gpio|gpio.mode=pulse|mode=poll|freq_hz=2|gpio.trig=16|gpio.echo=17|gpio.trig_us=10|gpio.pulse_timeout_us=25000|ENDCFG
```

**`gpio.mode=pulse` is required.** Without it the GPIO driver silently falls back to digital mode on a default pin: it never triggers the sensor, and its output looks like real data but is a pin state plus a millisecond counter that climbs steadily whatever the sensor does. To confirm the config was read correctly, check the `[SCHED …] add[0]:` line it prints: it must say `method=pulse`. If `method=` is blank, the mode is missing. The `GPIO(pin=…)` number on that line is not used in pulse mode and can be ignored.

After `RUN`, a working HC-SR04 prints lines like:

```
proto=gpio src=HCSR04 len=3 ts=12345 : 2A 06 01
```

The first two bytes are the echo pulse width in microseconds, low byte first, so `2A 06` is 0x062A = 1578 µs. The third byte `01` marks a valid reading. Divide the width by 58 to get the distance in centimetres, so 1578 µs is about 27 cm. A line reading `len=1 … : 00` means no echo arrived before the timeout.

Either way, new lines should arrive repeatedly at roughly the configured `freq_hz`, and the values should change when you move the sensor or your hand in front of it. That live, changing output is the real end-to-end proof: firmware built correctly, flashed correctly, the specific sensor is wired to the pins you told the config about, and the bus driver for that protocol is reading real data. Type `STOP` when done.

If you send the config and get `[SCHED] no free slots` or an `ERR:` line instead, re-check the syntax against `project_v3/README.md`; if you get `[SCHED] added:` but never see any data lines after `RUN`, re-check the physical wiring from section 3 before assuming the firmware is broken.

## 13. Known issues and open items

`project_v3/project_v3/` is a stray duplicate subtree left over from the original zip upload, including one leftover `cmake_install.cmake` build artifact. It isn't referenced by the build and can be safely ignored or deleted.

If your assigned board turns out to be an original Pico W (RP2040) rather than a Pico 2 W, this firmware will not run on it as currently configured — flag this to the team rather than assuming your hardware matches.

If the team's logic level shifter arrives, it can replace the Echo resistor divider in section 3. Keep the Pico side at 3.3 V logic and power the HC-SR04 as its own specification requires.

## ESP32-S3 Arduino setup (Tasks 7–9; local bring-up verified)

The ESP32-S3 port uses **Arduino IDE with the Arduino-ESP32 core**. The Pico instructions above and the root Pico `CMakeLists.txt` remain the reference build. The S3 sketch is in [`esp32s3/`](esp32s3/README.md) and is limited to serial bring-up. Do not treat its successful upload as framework parity.

### Arduino environment baseline (Task 7)

| Item | Project baseline |
| --- | --- |
| Arduino IDE version | **2.3.10** |
| Arduino-ESP32 core version | **3.3.12** (`esp32 by Espressif Systems` in Boards Manager) |
| Board profile / FQBN | **ESP32S3 Dev Module** / `esp32:esp32:esp32s3` with the Tools options recorded below |
| Required external Arduino libraries | **None for the minimal bring-up sketch.** Name and pin each library when a later task selects it. |

Sitt selected the locally tested versions above as the project baseline on 2026-10-13. All team members should install these exact versions and compile the same minimal S3 sketch with the board options below. Task 7 is complete only when every machine has passed that check; verification on the other machines is still outstanding. Arduino CLI 1.5.1 was present in the earlier local check, but the 2026-10-13 hardware test used the Arduino IDE workflow, so CLI is not part of this baseline.

**Local preliminary checks — Sitt, Windows (not the team pin):** On 2026-09-30, Arduino IDE 2.3.10, its bundled Arduino CLI 1.5.1, and Arduino-ESP32 core 3.3.10 were found on this machine. At commit `8197902`, `esp32s3/esp32s3.ino` compiled with Arduino CLI using the generic `esp32:esp32:esp32s3` profile and its default options.

On 2026-10-07, Arduino IDE 2.3.10 showed **esp32 by Espressif Systems 3.3.12 installed**. Its **Verify** action compiled the same sketch with **ESP32S3 Dev Module** selected: 273,621 bytes of program storage and 21,592 bytes of global variables were reported, with no compile error. The board was not connected during that check. The later physical upload and serial result is recorded below; Task 7 still needs the same build verified on the other team machines.

**Second machine — Hasif, macOS (arduino-cli 1.5.1, core 3.3.12), 2026-10-13:** the same minimal sketch with the same board options compiled to 273,317 bytes of program storage and 21,592 bytes of global variables, with no error. Globals match exactly; the 304-byte program difference is the host toolchain build.

**Clean install — Hasif, macOS, 2026-10-14:** a fresh clone of `s3-port` (commit `d351876`) from GitHub, built with arduino-cli using empty data and sketchbook directories (nothing pre-installed). `sh tools/s3.sh setup` installed core 3.3.12 and MQTT 2.5.3; `sh tools/s3.sh flash` built the firmware (1,331,494 bytes) and the upload hash verified; on the board 4a passed 14/14, 4b 25/25 and 5-neg 27/27. Any other machine checks itself with `sh tools/s3.sh check`: the versions must read 3.3.12 and 2.5.3, and the bring-up sketch must report 21,592 bytes of globals.

1. Install the agreed Arduino IDE version from the [Arduino software page](https://www.arduino.cc/en/software).
2. In Arduino IDE, open **File → Preferences** and add Espressif's stable Boards Manager URL to **Additional boards manager URLs**: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`.
3. Open **Tools → Board → Boards Manager**, find **esp32 by Espressif Systems**, and install version **3.3.12**. Select **ESP32S3 Dev Module** for the HW-747 board used in this bring-up and apply the recorded options below.
4. Install any later required external libraries through Library Manager at their recorded versions. The minimal sketch needs no external library.

The [Arduino-ESP32 installation guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html) documents the Boards Manager URL and installation flow.

### Board details and IDE settings to confirm (Task 8)

Record these from the actual board markings, its matching pinout and a physical USB test before selecting final settings or connecting sensors:

| Item | Confirmed value |
| --- | --- |
| Exact Super Mini board/revision and ESP32-S3 module marking | User confirmed the physical board matches the supplied [HW-747 Super Mini pinout diagram](Pinout_Diagram_S3.jpg) on 2026-10-13; no module can: the bare chip is an **ESP32-S3FH4R2** (QFN56, revision v0.2), identified electronically on 2026-10-14 by esptool 5.3.1 `flash-id` on macOS: `Embedded Flash 4MB (XMC), Embedded PSRAM 2MB (AP_3v3)`, 40 MHz crystal, flash ID 46/4016 |
| Flash size | 4 MB embedded flash, detected on COM6 with esptool v5.3.1 on 2026-10-13 |
| PSRAM presence and size | 2 MB embedded quad PSRAM (AP_3v3), reported by esptool; used by the firmware with PSRAM `QSPI PSRAM` (about 2.0 MB free at run time, `TASKS` output) |
| Exposed, usable GPIOs and reserved/unsafe pins | Candidate exposed pins are shown in the user-confirmed [HW-747 pinout diagram](Pinout_Diagram_S3.jpg); see the pin and power table below; enforced in firmware by `hal_gpio_valid` |
| USB connector and serial path (`Serial` via native USB CDC or USB-to-UART bridge) | USB-Serial/JTAG reported by esptool; the board enumerated on COM6 as `ESP32 Family Device, Ozobot DRVKit`, and sketch `Serial` output was observed on COM6 at 115200 baud |
| Arduino IDE board profile | `ESP32S3 Dev Module` used for local compile, upload, and Serial Monitor verification |
| Board-specific Flash Size and PSRAM menu choices | Flash Size: `4MB (32Mb)`; PSRAM: `Disabled` for the minimal bring-up sketch, despite 2 MB being present; the full firmware uses `QSPI PSRAM` (verified: Wi-Fi, MQTT and the tensor arena run from it) with Partition Scheme `Huge APP (3MB No OTA/1MB SPIFFS)` |
| Board-specific USB Mode, USB CDC On Boot and Upload Mode choices, if offered | USB-Serial/JTAG connection; USB CDC On Boot: `Enabled`; Upload Mode: `UART0 / Hardware CDC`; USB Mode `Hardware CDC and JTAG` (arduino-cli `USBMode=hwcdc`) |
| Port and any first-upload/boot-button procedure | COM6 used for upload and monitoring; no BOOT button needed on either machine (Windows COM6, macOS `/dev/cu.usbmodem*`); if an upload reports "serial TX path seems to be down", use another data cable or hold BOOT while plugging in |

The read-only probe was `esptool.exe --chip esp32s3 --port COM6 flash-id`; it did not upload or erase firmware. The exact USB and upload settings depend on the board's wiring and Arduino-ESP32 core version. Follow the matching board documentation and verify the result on hardware; do not assume that every ESP32-S3 Super Mini exposes the same console path. Espressif's [USB CDC guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/cdc_dfu_flash.html) explains the relevant Arduino menu options.

The Arduino IDE **Tools** menu shown after the successful local upload/monitor run also had these values: Flash Mode `QIO 80MHz`; Partition Scheme `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)`; Upload Speed `921600`; CPU Frequency `240MHz (WiFi)`; Events Run On `Core 1`; Arduino Runs On `Core 1`. USB DFU On Boot, USB Firmware MSC On Boot, JTAG Adapter, and Erase All Flash Before Sketch Upload were `Disabled`; Core Debug Level was `None`. Record any further menu options if the board profile exposes them.

### Pin, power and wiring (Task 8)

Front view, USB-C at the top ([pinout](Pinout_Diagram_S3.jpg)). Left column top to bottom: TX (GPIO43), RX (GPIO44), GPIO1–7. Right column: 5V, GND, 3V3, GPIO13–8. All GPIO are 3.3 V only and not 5 V tolerant.

| Pins | Use |
| --- | --- |
| GPIO1–2, 4–18, 21, 33–44, 47–48 | Usable. GPIO48 drives the on-board RGB LED |
| GPIO0, 3, 45, 46 | Strapping pins: leave unconnected at boot |
| GPIO19–20 | Native USB (console and upload): refused by the firmware |
| GPIO22–25 | Not present on the ESP32-S3: refused |
| GPIO26–32 | In-package flash and PSRAM: refused |
| 5V | USB VBUS, for 5 V sensors (HC-SR04, radar); total limited by the USB port (500 mA) |
| 3V3 | On-board regulator; the vendor gives no rating, so keep sensor load under 100 mA. GPIO source current up to 40 mA, 20 mA recommended |

**Final D1 wiring (no pin conflicts).** Every sensor is owned by SchedulerTask through the shared EdgeCore driver named; the HAL and drivers are Hasif's per the Week 6 review.

```text
ESP32-S3 Super Mini            Device                     Driver (SchedulerTask)
GPIO1  (SDA) ─────────────────  GY-511 SDA  (0x1E, 0x19)   bus/i2c.c
GPIO2  (SCL) ─────────────────  GY-511 SCL
3V3 ──────────────────────────  GY-511 VIN
GPIO4  ───────────────────────  HC-SR04 Trig               bus/gpio.c (pulse)
GPIO7  ──┬── 10k ─────────────  HC-SR04 Echo (5 V)
         └── 20k ── GND          (1:2 divider, 3.3 V max)
5V ───────────────────────────  HC-SR04 VCC, radar VCC
GPIO8  ───────────────────────  digital sensor DO (MH module, IR)  bus/gpio.c (digital/counter)
GPIO44 (RX) ──────────────────  radar TX                   bus/uart.c (Serial1)
GPIO43 (TX) ──────────────────  radar RX
GPIO5  ───────────────────────  PWM/LED output             rule actions (LEDC)
GND ──────────────────────────  every device GND
Test only: GPIO1↔GPIO2 (UART loopback, I2C removed), GPIO5↔GPIO6 (GPIO loopback)
```

Configs for each device are in the README sensor table. Verified so far on this board: GY-511 (`4c-mag`), loopback UART, GPIO and PWM; the HC-SR04 and radar need the divider and the radar module respectively.

### D2 SPI planning note

D2's verified loopback fixture uses SPI2/FSPI with SCLK GPIO11, MOSI GPIO12, MISO GPIO13, and CS GPIO10; connect GPIO12 directly to GPIO13 only for loopback and leave the TFT disconnected. The proposed W25Q32 fixture uses the same four signals plus 3.3 V and GND, but its module markings and power requirements must be checked before wiring. The earlier TFT experiment used SCLK GPIO12, MOSI GPIO11, CS GPIO10, DC GPIO9, reset GPIO8, and backlight GPIO13; it is a different, optional fixture and cannot coexist with the loopback/MISO wiring. Keep GPIO19/20 for native USB, keep boot/strapping pins available, and do not use flash/PSRAM-connected pins. Follow [D2_SPI_TODO.md](D2_SPI_TODO.md) for validation, fault, benchmark, and evidence gates.

### Compile, upload and monitor the minimal sketch (Task 9)

1. In Arduino IDE, open `esp32s3/esp32s3.ino` and select the confirmed **ESP32-S3 target board** and board-specific settings from the table above.
2. Connect the board with a data-capable USB cable and choose its observed port under **Tools → Port**.
3. Click **Verify/Compile**, then **Upload**. Record the IDE, core and board settings used for the result.
4. Open **Serial Monitor** at **115200 baud** on the confirmed console port. The sketch repeats `Hello from ESP32-S3`, so the line remains visible even if Serial Monitor opens after startup. If upload succeeds but the line is absent, check the actual USB/serial path and IDE USB settings before changing the sketch.

Task 9's local hardware check passes after the line is observed on the physical Super Mini. The pin table and PSRAM mode are now verified (task 8 section above).

**Local bring-up result — Sitt, Windows, 2026-10-13:** With Arduino IDE 2.3.10, Arduino-ESP32 core 3.3.12, `ESP32S3 Dev Module`, and COM6, the minimal sketch compiled and uploaded successfully. Arduino IDE reported that the written data hash was verified and completed the upload. Serial Monitor at 115200 baud showed `Hello from ESP32-S3` repeatedly at approximately one-second intervals. This verifies the sketch, upload, and console path on this board with the Tools menu selections recorded above. The retained observation record is [`parity/logs/9-arduino-bringup.txt`](parity/logs/9-arduino-bringup.txt); team-wide Task 7 verification remains outstanding.

### Port architecture for later tasks

```text
Shared Configurable Edge code
           |
          HAL
        /     \
   Pico HAL   ESP32-S3 HAL
       |            |
   Pico SDK   Arduino-ESP32
```

Configuration parsing, the common sensor representation, scheduler, rule engine, output formatting, MQTT payload handling and application/ML logic should not call Arduino or Pico SDK functions directly. Platform operations belong in the HAL or platform drivers. If partitions, PSRAM access, a compatible TFLite Micro integration or measured performance require underlying ESP-IDF APIs from Arduino, document each exception and keep it below the shared-code boundary. The MQTT library remains to be selected. For Task 20, Arduino-ESP32 3.3.12 bundles Espressif TFLite Micro 1.4.0; the S3 bridge links and runs the built-in model on the board. Other models and persistent upload still need verification. See the [Task 20 hardware check](parity/task20/README.md).

## Shared EdgeCore library and ESP32-S3 framework build (Tasks 12–13)

The framework's platform-independent code, including the GPIO, I2C and UART drivers, is one Arduino library, [`libraries/EdgeCore/`](libraries/EdgeCore/), and both targets compile it from that folder; there is no second copy. The Pico build lists its sources in the root `CMakeLists.txt` (`EDGECORE`). The ESP32-S3 platform layer is a second library, [`libraries/EdgeS3/`](libraries/EdgeS3/). The minimal `esp32s3/` bring-up sketch stays free of framework code.

| Folder | Contents |
| --- | --- |
| `libraries/EdgeCore/src/` | Shared code: configuration, scheduler, REPL, rules, bus drivers, MQTT payload handling, inference manager, and the HAL contract `hal/hal.h` |
| `project_v3/` | Pico 2 W platform code: `main.c`, lwIP MQTT client, flash model transfer, TFLite Micro wrapper, `platform/pico/hal_pico.c` |
| `libraries/EdgeS3/src/` | ESP32-S3 platform code: HAL backend, the FreeRTOS runtime from the Week 6 design review, and placeholders for services tasks 19–20 port |
| `esp32s3_edge/` | ESP32-S3 firmware sketch |
| `esp32s3_haltest/` | Task 12 HAL timing and console test sketch |

The Pico build steps in sections 9–11 are unchanged.

**Versions:** the task 7 baseline above (Arduino IDE 2.3.10, `esp32` core 3.3.12, no external libraries), with arduino-cli 1.5.1 for command-line builds. Board **ESP32S3 Dev Module** with USB Mode *Hardware CDC and JTAG*, USB CDC On Boot *Enabled*, Flash Size *4MB*, PSRAM *QSPI PSRAM*, Partition Scheme *Huge APP (3MB No OTA/1MB SPIFFS)*: with Wi-Fi, MQTT and TFLite Micro the firmware is about 1.29 MB, which fills 98% of the default 1.2 MB app partition, and the static tables and tensor arena leave under 15 KB of internal RAM, so allocations fall back to the board's 2 MB PSRAM. External library: **MQTT by Joel Gaehwiler 2.5.3** (Library Manager; with the sketchbook set to this repository it installs into `libraries/MQTT/`, which is gitignored). Wi-Fi settings go in `esp32s3_edge/wifi_credentials.h` (copy the `.example`; gitignored); the S3 joins 2.4 GHz WPA2-Personal only. On an iPhone hotspot keep the Personal Hotspot screen open and turn on Maximize Compatibility, or the S3 reports `network not found`.

### Terminal only (recommended)

`tools/s3.sh` wraps the Arduino CLI so no IDE is needed. Run it from the repository root:

```bash
sh tools/s3.sh setup              # once: core 3.3.12, MQTT 2.5.3, Python env, credentials file
sh tools/s3.sh flash              # build esp32s3_edge and upload it
sh tools/s3.sh monitor            # console at 115200 (Ctrl-C to leave)
sh tools/s3.sh test 4a            # any parity stage on the S3 (4a 4b 4c-uart 5-neg t02 t03 t04 t06 t08 t09 t10)
sh tools/s3.sh mqtt <broker-ip>   # 27-step MQTT test against node s3-001
sh tools/s3.sh haltest            # HAL timing, console, PWM, pulse and edge-count checks
```

Console commands on the S3, besides the REPL: `TASKS` (free stack per task and heap), `WIFISCAN` (networks the S3 can see and why it is not connected), `WIFIDROP` (drops Wi-Fi for 10 s to test reconnection). If an upload fails with "serial TX path seems to be down", replace the USB cable or hold BOOT while plugging in.

### Arduino IDE

1. Open **File → Preferences** and set **Sketchbook location** to the root of this repository. The IDE then finds `libraries/EdgeCore` and `libraries/EdgeS3` in place, with nothing copied.
2. Install the `esp32` core at the version above (see the task 7 steps), then open `esp32s3_edge/esp32s3_edge.ino` (or `esp32s3_haltest/esp32s3_haltest.ino` for the HAL test).
3. Select the board and options above under **Tools**, choose the S3's port, and click **Upload**. If the port does not appear or the upload fails, hold **BOOT** while plugging the board in, upload, then press **RESET**.
4. Open **Serial Monitor** at 115200 baud. The firmware answers the same REPL commands as the Pico (`HELP`, `SHOW`, `RUN`, ...) plus `TASKS`, which prints each RTOS task's free stack and the heap.

### Arduino CLI

```bash
arduino-cli config set board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.12
FQBN=esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=4M,PSRAM=enabled,PartitionScheme=huge_app
arduino-cli compile --fqbn $FQBN --libraries libraries esp32s3_edge
arduino-cli upload  --fqbn $FQBN --port /dev/cu.usbmodemXXXX esp32s3_edge
```

### Checks

`python3 parity/parity_test.py --stage <stage> --board s3 --port <port>` runs a parity stage on the S3; `--board s3` swaps the Pico pin numbers for the S3 pins (GP16→1, GP17→2, GP18→7, GP20→4, GP21→5, GP22→6, GP25→8), so the loopback jumpers go on S3 pins 1↔2 and 5↔6. `python3 parity/hal_test.py` runs the HAL timing and console checks against `esp32s3_haltest`. `sh tools/check_hal_boundary.sh` compiles every EdgeCore source with no SDK headers, which shows that no shared module bypasses the HAL. With both boards plugged in, always pass `--port`.

## Release (task 23)

Team action: agree first, then one person runs it. Merge `s3-port` into `main` and tag the release:

```sh
git checkout main && git pull origin main
git merge --no-ff s3-port -m "Merge s3-port: D1 ESP32-S3 port"
git tag -a d1-s3-release -m "D1: ESP32-S3 port"
git push origin main d1-s3-release
```

Check the release from a clean clone on any machine with the task 7 versions installed (board on USB, Mac on the same hotspot as the S3):

```sh
git clone --recursive git@github.com:junyonggoh2004/EMBSYS-P2-AAI15.git d1-check && cd d1-check
git checkout d1-s3-release
sh tools/s3.sh setup                     # core 3.3.12 and the MQTT library
cp esp32s3_edge/wifi_credentials.h.example esp32s3_edge/wifi_credentials.h   # then edit it
sh tools/s3.sh flash
sh tools/s3.sh test 4a && sh tools/s3.sh test 4b && sh tools/s3.sh test 4c-uart
```

The clean-clone part was run on 2026-10-14 from `s3-port` before the merge (see the task 7 clean install). Task 23 is done when the team merges, tags, and repeats these commands on the tag.

## ESP32-S3 on µT-Kernel 3 SMP (tasks 25–34)

Since Tribunal 1 the D1 firmware runs on the course µT-Kernel 3 SMP port instead of Arduino-ESP32 and FreeRTOS ([MTK_REDESIGN.md](MTK_REDESIGN.md)). The project is [`esp32s3_mtk/`](esp32s3_mtk/), a native ESP-IDF project; the Arduino build above stays as the FreeRTOS baseline.

### Pinned versions (task 25)

| Item | Version | Why pinned |
|---|---|---|
| Course port `sirfonzie/mtk3smp-esp32s3` | commit `e92874d` (submodule `esp32s3_mtk/third_party/mtk3smp-esp32s3`, unmodified) | The course port named in the change requirement |
| ESP-IDF | `v6.1-dev-2300-g17a74c925c` (commit `17a74c925c`) | The exact revision the port is qualified on; it replaces IDF's FreeRTOS scheduler, so another revision would need its qualification rerun |
| Compiler | `xtensa-esp-elf-gcc 15.2.0` (`esp-15.2.0_20251204`), installed by IDF's `install.sh` | Same reason |
| esp-mqtt | `espressif/mqtt` 1.1.0, locked in `esp32s3_mtk/dependencies.lock` | esp-mqtt left IDF for the component registry in v6 |
| TFLite Micro | `espressif/esp-tflite-micro` 1.4.0, locked in the same file | The release Arduino-ESP32 3.3.12 bundles, so inference compares like for like with the FreeRTOS build |

`sh tools/mtk.sh setup` clones ESP-IDF into `~/esp/esp-idf-v6.1` (set `IDF_DIR` to change it), checks out the pinned commit and its submodules, installs the toolchain for the ESP32-S3, creates the Python test environment and copies the credentials template. `sh tools/mtk.sh check` prints the three pinned versions, refuses a mismatch, and builds the firmware.

```sh
git submodule update --init esp32s3_mtk/third_party/mtk3smp-esp32s3
sh tools/mtk.sh setup        # once, about 15 minutes and 2 GB
sh tools/mtk.sh check        # course port, ESP-IDF and compiler versions, then a build
sh tools/mtk.sh flash        # build and flash over the board's USB-C port
sh tools/mtk.sh monitor      # console (Ctrl-] to leave)
```

Wi-Fi settings go in `esp32s3_mtk/main/wifi_credentials.h` (copy the `.example`; gitignored), with the same fields as the Arduino build. `MQTT_HOST` is compiled in, so after the laptop changes network (a different hotspot or router gives it a new IP address) put the new address there and flash again.

### Testing on µT-Kernel (tasks 27–32)

The parity scripts are the same as for the Arduino build; `tools/mtk.sh` passes them the board and writes the logs to `parity/logs/mtk/`. Results are in [PARITY.md §4](parity/PARITY.md).

```sh
sh tools/mtk.sh haltest                  # HAL test firmware, then the normal firmware is flashed back
sh tools/mtk.sh test 4a                  # any wiring-free stage: 4a, 5-neg, t03, t04, t06, t10
sh tools/mtk.sh compare 4a               # the Pico's and this build's output for the stage
sh tools/mtk.sh mqtt <laptop-ip>         # also: model, flood, endurance
MOSQ_CONF=~/mosq.conf sh tools/mtk.sh faults <laptop-ip>
sh tools/mtk.sh timing                   # NFR1
sh tools/mtk.sh wcet                     # SchedulerTask cycle time
sh tools/mtk.sh purity                   # no FreeRTOS, Arduino or esp_driver_* use in our code
```

For the broker-loss part of `faults`, start Mosquitto as `/opt/homebrew/sbin/mosquitto -c ~/mosq.conf -v` (the test stops and restarts it by that name) and set `MOSQ_CONF`; without it the other eight checks still run. Keep the laptop awake and its lid open during `endurance` (30 minutes): on battery, a closed lid puts the Mac to sleep, the test stops reading the board and the broker goes offline. If `flash` reports "No serial data received" or "TX path seems down", `mtk.sh` resets the board and retries once by itself. When a board goes back from the µT-Kernel build to the Arduino build, erase the flash first (`idf.py -C esp32s3_mtk -p <port> erase-flash` after `. ~/esp/esp-idf-v6.1/export.sh`): the µT-Kernel `model` partition overlaps the Arduino build's LittleFS partition, and a stored model would sit inside the Arduino file system. The other direction needs nothing, because the µT-Kernel model store ignores any slot without its CRC-checked header.

Two behaviours of the course port affect testing (MTK_REDESIGN.md §8 and §10): its radio sometimes stops receiving for a minute or more, including right after a reset, so a network test that runs during such a window fails until the link returns; and the port's kernel and shim must run from IRAM for flash writes to be safe, which `components/edge_mtk/port_iram.lf` arranges.
