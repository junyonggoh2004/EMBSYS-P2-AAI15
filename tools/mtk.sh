#!/bin/sh
# µT-Kernel 3 SMP build of the ESP32-S3 firmware (D1 tasks 25-34), from the
# terminal. Run from the repo root (macOS/Linux Terminal, or Git Bash on Windows).
#
#   sh tools/mtk.sh setup              one-time: ESP-IDF at the pinned commit, its toolchain, Python test env
#   sh tools/mtk.sh check              print the pinned versions and build the firmware
#   sh tools/mtk.sh build              build esp32s3_mtk
#   sh tools/mtk.sh flash              build and flash
#   sh tools/mtk.sh monitor            open the console (Ctrl+C to leave)
#   sh tools/mtk.sh haltest            flash the HAL test firmware, run it, then restore the firmware
#   sh tools/mtk.sh purity             check our code uses no FreeRTOS, Arduino or esp_driver_* API
#   sh tools/mtk.sh image              check the built firmware links no FreeRTOS kernel code
#   sh tools/mtk.sh test <stage>       run a parity stage, e.g. 4a, 5-neg, t04; log in parity/logs/mtk/
#   sh tools/mtk.sh compare <stage>    the Pico's and this build's output for a stage, side by side
#   sh tools/mtk.sh live sonic|mag [s] run the HC-SR04 or the GY-511 and show its readings in units
#   sh tools/mtk.sh mqtt <broker-ip>   the 27-step MQTT test (node from wifi_credentials.h)
#   sh tools/mtk.sh model|faults|flood|endurance <broker-ip>
#                                      faults includes the broker-loss check when MOSQ_CONF names the
#                                      Mosquitto config and Mosquitto runs as /opt/homebrew/sbin/mosquitto -c it
#   sh tools/mtk.sh timing             NFR1 sample period and jitter
#   sh tools/mtk.sh wcet               SchedulerTask cycle time per scenario
#   sh tools/mtk.sh spi                D2 SPI REPL/loopback suite (GPIO12 to GPIO13 jumper)
#   sh tools/mtk.sh spi-fault          D2 connected SPI faults (same jumper)
#   sh tools/mtk.sh spi-bench [mode] [hz]  8–1024-byte µT-Kernel SPI benchmark
#   SPIID (serial console)             read-only flash JEDEC ID, after checking wiring
#   SPICHECK <id>, SPISCAN <id> (serial console)
#                                      read-only W25Q32 repeated read and last-sector scan
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
# Board-independent commands (purity/build/image) also run before pyserial is
# installed. A failed optional discovery must not exit the set -e wrapper.
PORT=${PORT:-$($PY -c "from serial.tools import list_ports; print(next((p.device for p in list_ports.comports() if p.vid == 0x303A), ''))" 2>/dev/null || true)}

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
  # The console takes typed lines (HELP, TASKS, BEGINCFG...) through pyserial's
  # miniterm: ESP-IDF's monitor shows the board's output but did not pass typed
  # keys to it in our tests. For decoded crash backtraces (output only) use
  # idf.py -C esp32s3_mtk monitor.
  monitor) need_port; echo "Console on $PORT: type a command and press Enter. Ctrl+C leaves."
           $PY -m serial.tools.miniterm --eol CRLF --raw --quiet --exit-char 3 "$PORT" 115200 ;;
  haltest) need_port; mkdir -p parity/logs/mtk
           idf_env
           flash_proj $PROJ/haltest >/dev/null
           trap 'echo "Restoring the firmware..."; flash_proj $PROJ >/dev/null' EXIT
           sleep 2
           $PY parity/hal_test.py --port "$PORT" --log parity/logs/mtk/hal_test.log ;;
  purity)  sh tools/check_mtk_purity.sh || exit 1
           sh tools/check_hal_boundary.sh >/dev/null 2>&1 || { echo "FAIL  shared code calls an SDK: run sh tools/check_hal_boundary.sh"; exit 1; }
           echo "ok    shared code: no SDK calls (check_hal_boundary.sh)"
           if [ -f $PROJ/build/$PROJ.elf ]; then idf_env; sh tools/check_mtk_image.sh; else echo "note  build first to check the firmware image too"; fi ;;
  image)   idf_env; sh tools/check_mtk_image.sh ;;
  test)    need_port; mkdir -p parity/logs/mtk
           $PY parity/parity_test.py --stage "${2:-4a}" --board s3 --port "$PORT" --log "parity/logs/mtk/${2:-4a}.log" ;;
  live)    need_port; [ -n "$2" ] || { echo "usage: sh tools/mtk.sh live sonic|mag [seconds]"; exit 1; }
           $PY tools/mtk_live.py "$2" --port "$PORT" ${3:+--seconds "$3"} ;;
  compare) st=${2:-4a}
           for f in parity/logs/$st.log parity/logs/mtk/$st.log; do
             echo "===== $f"
             cut -c10- "$f" | grep -E "^(Commands:|  [A-Z]|CFG:|RULE:|CLEARED|RUN$|STOP$|\[SHOW\]|\[RULE\] [A-Z_0-9]+:|\[INFER\] invoke|proto=)" | awk '!seen[substr($0,1,40)]++' | head -${LINES_MAX:-25}
           done ;;
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
  spi)     need_port; mkdir -p parity/logs/mtk/d2-spi
           $PY parity/spi_test.py --port "$PORT" --log parity/logs/mtk/d2-spi/spi.log ;;
  spi-fault) need_port; mkdir -p parity/logs/mtk/d2-spi
           $PY parity/spi_fault_test.py --port "$PORT" --log parity/logs/mtk/d2-spi/fault-connected.log ;;
  spi-bench) need_port
           $PY parity/spi_benchmark.py --runtime mtk --port "$PORT" --mode "${2:-0}" --hz "${3:-10000000}" ;;
  *)       sed -n "2,$(grep -n '^# PORT=' "$0" | cut -d: -f1)p" "$0" ;;
esac
