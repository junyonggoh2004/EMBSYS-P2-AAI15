#!/bin/sh
# ESP32-S3 from the terminal: no Arduino IDE needed. Run from the repo root
# (macOS/Linux Terminal, or Git Bash on Windows).
#
#   sh tools/s3.sh setup             one-time: core, MQTT library, Python env
#   sh tools/s3.sh check             task 7: check versions and compile the bring-up and full sketches
#   sh tools/s3.sh compare <stage>   show the Pico and S3 logs of a stage side by side
#   sh tools/s3.sh flash             build esp32s3_edge and upload it
#   sh tools/s3.sh monitor           open the console (Ctrl-C to leave)
#   sh tools/s3.sh test <stage>      run a parity stage on the S3, e.g. 4a, 4b, 5-neg
#   sh tools/s3.sh mqtt <broker-ip>  run the 27-step MQTT test (node s3-001)
#   sh tools/s3.sh haltest           flash the HAL test sketch and run it
#   sh tools/s3.sh spi               run the D2 SPI validation and loopback suite
#   sh tools/s3.sh spi-bench [mode] [hz]  benchmark 8..1024 bytes, then restore production firmware
#   sh tools/s3.sh model <broker-ip>     model upload, bad uploads, reboot (task 20)
#   sh tools/s3.sh faults <broker-ip>    malformed/oversized input, Wi-Fi and broker loss
#   sh tools/s3.sh flood <broker-ip>     MQTT queue saturation
#   sh tools/s3.sh timing                NFR1 sample period and jitter (about 4 min)
#   sh tools/s3.sh endurance <broker-ip> NFR6 30-minute run
#
# The port is found by the board's Espressif USB ID; set PORT=... to override.
# Wi-Fi details go in esp32s3_edge/wifi_credentials.h (copy the .example).
set -e
FQBN=esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=4M,PSRAM=enabled,PartitionScheme=huge_app
# Python: the venv made by setup (bin/ on macOS and Linux, Scripts/ on Windows)
if python3 -c "" 2>/dev/null; then SYSPY=python3; else SYSPY=python; fi
PY=.venv/bin/python
[ -x "$PY" ] || PY=.venv/Scripts/python.exe
[ -x "$PY" ] || PY=$SYSPY
PORT=${PORT:-$($PY -c "from serial.tools import list_ports; print(next((p.device for p in list_ports.comports() if p.vid == 0x303A), ''))" 2>/dev/null)}

need_port() { [ -n "$PORT" ] || { echo "No ESP32-S3 found on USB. Plug it in (or set PORT=/dev/cu.usbmodemXXXX, or PORT=COM5 on Windows)."; exit 1; }; }
build() { arduino-cli compile --fqbn "$FQBN" --libraries libraries "$1"; }
upload() { need_port; arduino-cli upload --fqbn "$FQBN" --port "$PORT" "$1"; sleep 3; }

case "$1" in
  setup)
    arduino-cli config set board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
    arduino-cli core update-index
    arduino-cli core install esp32:esp32@3.3.12
    arduino-cli lib install "MQTT@2.5.3"
    $SYSPY -m venv .venv
    VPY=.venv/bin/python; [ -x "$VPY" ] || VPY=.venv/Scripts/python.exe
    $VPY -m pip install -q pyserial paho-mqtt
    [ -f esp32s3_edge/wifi_credentials.h ] || cp esp32s3_edge/wifi_credentials.h.example esp32s3_edge/wifi_credentials.h
    echo "Done. Edit esp32s3_edge/wifi_credentials.h, then: sh tools/s3.sh flash" ;;
  check)   # Task 7: every machine must match these and compile both sketches.
           echo "arduino-cli: $(arduino-cli version | sed 's/.*Version: \([^ ]*\).*/\1/')   (recorded 1.5.1)"
           echo "esp32 core:  $(arduino-cli core list | awk '$1=="esp32:esp32"{print $2}')   (recorded 3.3.12)"
           echo "MQTT lib:    $(arduino-cli lib list --libraries libraries 2>/dev/null | awk '$1=="MQTT"{print $2}')$(arduino-cli lib list 2>/dev/null | awk '$1=="MQTT"{print $2}')   (recorded 2.5.3)"
           echo "--- bring-up sketch, esp32:esp32:esp32s3 defaults (recorded: 21,592 B globals)"
           arduino-cli compile --fqbn esp32:esp32:esp32s3 esp32s3 | grep -E "Sketch uses|Global"
           echo "--- full firmware, $FQBN"
           build esp32s3_edge | grep -E "Sketch uses|Global" ;;
  compare) st=${2:-4a}
           for b in pico s3; do
             f=parity/logs/$st.log; [ $b = s3 ] && f=parity/logs/s3/$st.log
             echo "===== $(echo $b | tr a-z A-Z): $f"
             cut -c10- "$f" | grep -E "^(Commands:|  [A-Z]|CFG:|RULE:|CLEARED|RUN$|STOP$|\[SHOW\]|\[RULE\] [A-Z_0-9]+:|\[INFER\] invoke|proto=)" | awk '!seen[substr($0,1,40)]++' | head -${LINES_MAX:-25}
           done ;;
  build)   build esp32s3_edge ;;
  flash)   build esp32s3_edge && upload esp32s3_edge ;;
  monitor) need_port; arduino-cli monitor --port "$PORT" --config baudrate=115200 ;;
  test)    need_port; $PY parity/parity_test.py --stage "${2:-4a}" --board s3 --port "$PORT" --log "parity/logs/s3/${2:-4a}.log" ;;
  mqtt)    need_port; [ -n "$2" ] || { echo "usage: sh tools/s3.sh mqtt <broker-ip>"; exit 1; }
           $PY parity/mqtt_test.py --broker "$2" --node "${NODE:-s3-001}" --board s3 --port "$PORT" ;;
  haltest) build esp32s3_haltest && upload esp32s3_haltest && $PY parity/hal_test.py --port "$PORT" ;;
  spi)     need_port; $PY parity/spi_test.py --port "$PORT" ;;
  spi-bench)
           need_port
           build esp32s3_spibench
           trap 'echo "Restoring production firmware..."; build esp32s3_edge && upload esp32s3_edge' EXIT
           upload esp32s3_spibench
           $PY parity/spi_benchmark.py --port "$PORT" --mode "${2:-0}" --hz "${3:-10000000}" ;;
  model|faults|flood|endurance)
           need_port; [ -n "$2" ] || { echo "usage: sh tools/s3.sh $1 <broker-ip>"; exit 1; }
           case "$1" in
             model)     $PY parity/model_test.py --broker "$2" --port "$PORT" ;;
             faults)    $PY parity/net_fault_test.py --broker "$2" --port "$PORT" ;;
             flood)     $PY parity/flood_test.py --broker "$2" --node "${NODE:-s3-001}" --board s3 ;;
             endurance) $PY parity/endurance_test.py --broker "$2" --port "$PORT" ;;
           esac ;;
  timing)  need_port; $PY parity/timing_test.py --board s3 --port "$PORT" ;;
  *)       sed -n '2,20p' "$0" ;;
esac
