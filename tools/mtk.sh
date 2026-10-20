#!/bin/sh
# µT-Kernel 3 SMP build of the ESP32-S3 firmware (D1 tasks 25-34), from the
# terminal. Run from the repo root (macOS/Linux Terminal, or Git Bash on Windows).
#
#   sh tools/mtk.sh setup              one-time: ESP-IDF at the pinned commit, its toolchain, Python test env
#   sh tools/mtk.sh check              print the pinned versions and build the firmware
#   sh tools/mtk.sh build              build esp32s3_mtk
#   sh tools/mtk.sh flash              build and flash
#   sh tools/mtk.sh monitor            open the console (Ctrl-] to leave)
#   sh tools/mtk.sh haltest            flash the HAL test firmware, run it, then restore the firmware
#   sh tools/mtk.sh purity             check our code uses no FreeRTOS, Arduino or esp_driver_* API
#   sh tools/mtk.sh test <stage>       run a parity stage, e.g. 4a, 5-neg, t04; log in parity/logs/mtk/
#   sh tools/mtk.sh mqtt <broker-ip>   the 27-step MQTT test (node from wifi_credentials.h)
#   sh tools/mtk.sh model|faults|flood|endurance <broker-ip>
#                                      faults includes the broker-loss check when MOSQ_CONF names the
#                                      Mosquitto config and Mosquitto runs as /opt/homebrew/sbin/mosquitto -c it
#   sh tools/mtk.sh timing             NFR1 sample period and jitter
#   sh tools/mtk.sh wcet               SchedulerTask cycle time per scenario
#
# The course port is the submodule esp32s3_mtk/third_party/mtk3smp-esp32s3,
# pinned to the commit the course published. It is qualified on exactly the
# ESP-IDF and compiler versions below, so setup installs those and check
# refuses anything else. Wi-Fi details go in esp32s3_mtk/main/wifi_credentials.h
# (copy the .example). The port is found by the board's Espressif USB ID; set
# PORT=... to override.
set -e
IDF_COMMIT=17a74c925caeaf43075e48ff5cbb8a5c7e506fa1
IDF_DESCRIBE=v6.1-dev-2300-g17a74c925c
GCC_VERSION=esp-15.2.0_20251204
PORT_COMMIT=e92874d50d3a2968566f9628018f4c096c01f3d9
IDF_DIR=${IDF_DIR:-$HOME/esp/esp-idf-v6.1}
PROJ=esp32s3_mtk

if python3 -c "" 2>/dev/null; then SYSPY=python3; else SYSPY=python; fi
PY=.venv/bin/python
[ -x "$PY" ] || PY=.venv/Scripts/python.exe
[ -x "$PY" ] || PY=$SYSPY
PORT=${PORT:-$($PY -c "from serial.tools import list_ports; print(next((p.device for p in list_ports.comports() if p.vid == 0x303A), ''))" 2>/dev/null)}

need_port() { [ -n "$PORT" ] || { echo "No ESP32-S3 found on USB. Plug it in (or set PORT=/dev/cu.usbmodemXXXX, or PORT=COM5 on Windows)."; exit 1; }; }
idf_env() {
  [ -f "$IDF_DIR/export.sh" ] || { echo "ESP-IDF not found at $IDF_DIR. Run: sh tools/mtk.sh setup"; exit 1; }
  . "$IDF_DIR/export.sh" >/dev/null 2>&1
}
idf() { idf_env; idf.py -C "$PROJ" "$@"; }
# Reset the chip into its normal firmware through the USB-Serial-JTAG RTS line.
reset_chip() {
  $PY -c "import serial,time,sys
s=serial.Serial(sys.argv[1],115200,timeout=0.1); s.dtr=False; s.rts=True; time.sleep(0.1)
try: s.rts=False
except Exception: pass" "$PORT" 2>/dev/null || true
}
# Flash a project. esptool sometimes reaches the ROM download mode without a
# sync reply ("TX path seems down"); a normal reset and a 10 s wait for the
# USB device to come back clears it, so retry once that way.
flash_proj() {
  idf.py -C "$1" -p "$PORT" build >/dev/null
  idf.py -C "$1" -p "$PORT" flash && return 0
  echo "Flash failed; resetting the board and retrying in 10 s..."
  reset_chip; sleep 10
  idf.py -C "$1" -p "$PORT" flash
}
node_id() { sed -n 's/^#define NODE_ID *"\([^"]*\)".*/\1/p' $PROJ/main/wifi_credentials.h 2>/dev/null | head -1; }

case "$1" in
  setup)
    git submodule update --init esp32s3_mtk/third_party/mtk3smp-esp32s3
    if [ ! -d "$IDF_DIR/.git" ]; then
      mkdir -p "$(dirname "$IDF_DIR")"
      git clone --filter=blob:none --no-checkout https://github.com/espressif/esp-idf.git "$IDF_DIR"
    fi
    git -C "$IDF_DIR" checkout -q $IDF_COMMIT
    git -C "$IDF_DIR" submodule update --init --recursive --depth 1
    "$IDF_DIR/install.sh" esp32s3
    $SYSPY -m venv .venv
    VPY=.venv/bin/python; [ -x "$VPY" ] || VPY=.venv/Scripts/python.exe
    $VPY -m pip install -q pyserial paho-mqtt
    [ -f $PROJ/main/wifi_credentials.h ] || cp $PROJ/main/wifi_credentials.h.example $PROJ/main/wifi_credentials.h
    echo "Done. Edit $PROJ/main/wifi_credentials.h, then: sh tools/mtk.sh flash" ;;
  check)
    idf_env
    have_port=$(git -C $PROJ/third_party/mtk3smp-esp32s3 rev-parse HEAD 2>/dev/null || echo missing)
    have_idf=$(git -C "$IDF_DIR" describe --tags 2>/dev/null || echo missing)
    have_gcc=$(xtensa-esp-elf-gcc --version 2>/dev/null | head -1 | sed 's/.*(crosstool-NG \([^)]*\)).*/\1/')
    echo "course port: $have_port   (pinned $PORT_COMMIT)"
    echo "ESP-IDF:     $have_idf   (pinned $IDF_DESCRIBE)"
    echo "compiler:    $have_gcc   (pinned $GCC_VERSION)"
    [ "$have_port" = "$PORT_COMMIT" ] && [ "$have_idf" = "$IDF_DESCRIBE" ] && [ "$have_gcc" = "$GCC_VERSION" ] || {
      echo "MISMATCH: run sh tools/mtk.sh setup (and git submodule update)"; exit 1; }
    idf.py -C $PROJ build | grep -E "binary size|Project build complete" ;;
  build)   idf build ;;
  flash)   need_port; idf_env; flash_proj $PROJ ;;
  monitor) need_port; idf -p "$PORT" monitor ;;
  haltest) need_port; mkdir -p parity/logs/mtk
           idf_env
           flash_proj $PROJ/haltest >/dev/null
           trap 'echo "Restoring the firmware..."; flash_proj $PROJ >/dev/null' EXIT
           sleep 2
           $PY parity/hal_test.py --port "$PORT" --log parity/logs/mtk/hal_test.log ;;
  purity)  sh tools/check_mtk_purity.sh && sh tools/check_hal_boundary.sh >/dev/null 2>&1 && echo "ok    shared code: no SDK calls (check_hal_boundary.sh)" ;;
  test)    need_port; mkdir -p parity/logs/mtk
           $PY parity/parity_test.py --stage "${2:-4a}" --board s3 --port "$PORT" --log "parity/logs/mtk/${2:-4a}.log" ;;
  mqtt)    need_port; [ -n "$2" ] || { echo "usage: sh tools/mtk.sh mqtt <broker-ip>"; exit 1; }
           mkdir -p parity/logs/mtk
           $PY parity/mqtt_test.py --broker "$2" --node "${NODE:-$(node_id)}" --board s3 --port "$PORT" --log parity/logs/mtk/mqtt.log ;;
  model|faults|flood|endurance)
           need_port; [ -n "$2" ] || { echo "usage: sh tools/mtk.sh $1 <broker-ip>"; exit 1; }
           mkdir -p parity/logs/mtk
           case "$1" in
             model)     $PY parity/model_test.py --broker "$2" --node "${NODE:-$(node_id)}" --port "$PORT" --log parity/logs/mtk/model.log ;;
             faults)    $PY parity/net_fault_test.py --broker "$2" --node "${NODE:-$(node_id)}" --port "$PORT" \
                          ${MOSQ_CONF:+--mosquitto-conf "$MOSQ_CONF"} --log parity/logs/mtk/faults.log ;;
             flood)     $PY parity/flood_test.py --broker "$2" --node "${NODE:-$(node_id)}" --board s3 --seconds 60 --log parity/logs/mtk/flood.log ;;
             endurance) $PY parity/endurance_test.py --broker "$2" --node "${NODE:-$(node_id)}" --port "$PORT" --log parity/logs/mtk/endurance.log ;;
           esac ;;
  timing)  need_port; mkdir -p parity/logs/mtk; $PY parity/timing_test.py --board s3 --port "$PORT" --log parity/logs/mtk/timing.log ;;
  wcet)    need_port; mkdir -p parity/logs/mtk; $PY parity/wcet_test.py --port "$PORT" --log parity/logs/mtk/wcet.log ;;
  *)       sed -n '2,25p' "$0" ;;
esac
