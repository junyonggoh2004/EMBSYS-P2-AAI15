# Building, Flashing, and Verifying project_v3 (FlexiEdgePico)

This is the complete guide for getting the existing sensor node firmware running on your own hardware, from an empty machine to a board that is confirmed working. It covers what to install, what to wire, how to build, how to flash, and — critically — how to actually prove each stage worked rather than just assuming it did. Read it in order the first time through.

Verified end to end on macOS (Apple Silicon) on 2026-09-18. Windows and Linux notes are marked wherever a step differs.

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
