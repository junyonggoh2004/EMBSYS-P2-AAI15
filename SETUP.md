# Building and Flashing project_v3 (FlexiEdgePico)

This guide covers getting the existing sensor node firmware building and running on your own machine. Follow it in order. It was verified end to end on macOS (Apple Silicon) on 2026-09-18; Windows and Linux notes are marked where a step differs.

The firmware in this repository targets the **Raspberry Pi Pico 2 W (RP2350)**, set in `CMakeLists.txt` via `PICO_BOARD=pico2_w`. It will not run on the original Pico W (RP2040). Confirm you have the correct board before starting.

## 1. Install prerequisites

You need:

- Git
- CMake 3.13 or later
- Python 3
- GNU Make (on macOS, Apple's bundled `make` is too old; install a current one via Homebrew and make sure it comes first on your PATH)
- The Arm GNU Toolchain, version **14.2.Rel1** specifically (see step 4 for why the version matters)

## 2. Get the Pico SDK

Clone the Pico SDK and its submodules somewhere outside this repository, for example `~/pico/pico-sdk`:

```
git clone https://github.com/raspberrypi/pico-sdk.git ~/pico/pico-sdk
cd ~/pico/pico-sdk
git submodule update --init
```

You need SDK version 2.x or later. Version 1.5.x does not know about the `pico2_w` board and will fail to configure. Check with `git -C ~/pico/pico-sdk describe --tags`.

Set the SDK path as an environment variable so CMake can find it. Add this to your shell profile (`~/.zshrc` or `~/.bashrc` on macOS/Linux; a permanent environment variable on Windows):

```
export PICO_SDK_PATH=$HOME/pico/pico-sdk
```

## 3. Clone this repository and check out the branch

```
git clone <this repo's URL>
cd EMBSYS-P2-AAI15
git checkout Pico_Sample
```

Everything below happens inside this checkout.

## 4. Get the pinned toolchain (Arm GNU Toolchain 14.2.Rel1)

`CMakeLists.txt` pins `toolchainVersion 14_2_Rel1`, and this is not a formality. The inference engine bundles a snapshot of Google's flatbuffers headers that fails to compile under GCC 15 with a template deduction error. GCC 14.2 compiles it correctly. If your system's `arm-none-eabi-gcc` is already 14.x, you can skip to step 5 and use it directly. If it is 15.x or you are not sure, install 14.2.Rel1 separately rather than downgrading your system toolchain, since you may need the newer one for other work.

Download the archive for your platform from Arm's developer site (search "Arm GNU Toolchain 14.2.Rel1 downloads" if the direct link below has moved):

```
# macOS, Apple Silicon
curl -L -o toolchain.tar.xz "https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi.tar.xz"
```

Extract it to a fixed location, for example `~/.arm-toolchains/`:

```
mkdir -p ~/.arm-toolchains
tar xf toolchain.tar.xz -C ~/.arm-toolchains
```

For Windows or Intel Mac or Linux, use the matching archive name for your platform from the same Arm downloads page (`darwin-x86_64`, `x86_64-arm-none-eabi`, `mingw-w64-i686-arm-none-eabi`, etc). Do not put this toolchain's `bin` directory on your system PATH; it is referenced explicitly in step 6 instead, so it will not interfere with any other toolchain you already have installed.

## 5. Restore the missing TFLite Micro submodule

The firmware depends on a TFLite Micro port that should be a git submodule at `project_v3/third_party/pico-tflmicro`, but the submodule reference was lost when this project was originally uploaded as a zip file. Add it back:

```
git submodule add https://github.com/raspberrypi/pico-tflmicro.git project_v3/third_party/pico-tflmicro
```

If `.gitmodules` already exists in your checkout by the time you read this (because someone has already pushed this fix), use `git submodule update --init` instead.

## 6. Create your WiFi and MQTT credentials file

```
cp project_v3/include/wifi_credentials.h.example project_v3/include/wifi_credentials.h
```

Edit `project_v3/include/wifi_credentials.h` and fill in your WiFi SSID, password, and MQTT broker IP address. This file is gitignored; never commit it. Placeholder values are enough to build and flash if you only want to confirm the board boots — the firmware logs a WiFi connection failure and continues rather than halting.

## 7. Configure the build

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

Adjust the three compiler paths to match wherever you extracted the toolchain in step 4, and to your platform's executable names (`arm-none-eabi-gcc.exe` on Windows). If your system's default `arm-none-eabi-gcc` is already 14.x, you can drop these three flags entirely and just run `cmake -DCMAKE_BUILD_TYPE=Release ..`.

The first configure will download and build `picotool` from source if you do not already have a matching version installed; this is expected and only happens once.

## 8. Build

```
make -j$(sysctl -n hw.ncpu)
```

On Linux, use `make -j$(nproc)`. On Windows with the Ninja generator, use `ninja` instead of `make`. A successful build ends with `Built target project_v3` and produces `project_v3.uf2` in the `build` directory.

## 9. Flash the board

Hold the BOOTSEL button on the Pico 2 W itself (next to the micro-USB or USB-C port, not any carrier board button) while plugging it into your computer. It will mount as a USB mass storage drive named `RP2350`. Drag `build/project_v3.uf2` onto that drive. The board will reboot automatically and start running the firmware.

Open a serial terminal to the board's USB port (baud rate does not matter for CDC USB serial) and set the terminal to CRLF line endings. You should see WiFi connection status printed, followed by REPL output. Sensor and rule configuration syntax is documented in the top-level `README.md`.

## Known issues

The `project_v3/project_v3/` subdirectory is a stray duplicate left over from the original zip upload, including a leftover `cmake_install.cmake` build artifact. It is not used by the build and can be ignored or deleted.

If you are on the original Pico W (RP2040) rather than a Pico 2 W, this firmware will not run as configured. Raise this with the team before assuming your hardware matches what the build expects.
