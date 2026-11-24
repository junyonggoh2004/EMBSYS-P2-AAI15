#!/bin/sh
# Runs every laptop test in the repository, one line per suite, and writes the
# results to parity/logs/host/<date>/. No board is needed.
#   sh tools/run_all_tests.sh            the laptop suites (about 30 s)
#   BUILD=1 sh tools/run_all_tests.sh    also builds the µT-Kernel firmware for
#                                        the N16R8 and checks the linked image
#                                        (needs ESP-IDF; see SETUP.md)
# The test cases and what each one proves are listed in TESTING.md.
set -u
cd "$(dirname "$0")/.."
[ -x .venv/bin/python ] && PATH="$PWD/.venv/bin:$PATH" && export PATH
PY=$(command -v python3 || command -v python)
OUT=${OUT:-parity/logs/host/$(date +%Y%m%d-%H%M)}
mkdir -p "$OUT"
pass=0; fail=0
run() {
  name=$1; shift
  if "$@" > "$OUT/$name.log" 2>&1; then
    line="PASS  $name"; pass=$((pass + 1))
  else
    line="FAIL  $name  (see $OUT/$name.log)"; fail=$((fail + 1))
  fi
  echo "$line"; echo "$line" >> "$OUT/summary.txt"
}
{
  echo "Laptop test run, $(date '+%d %b %Y %H:%M'), commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "Compiler: $(${CC:-cc} --version 2>/dev/null | head -1); Python: $($PY --version 2>&1)"
} > "$OUT/summary.txt"
run d1-hal-boundary   sh tools/check_hal_boundary.sh
run d1-mtk-purity     sh tools/check_mtk_purity.sh
run d1-startup        sh tools/startup_test.sh
run d1-runtime        sh tools/mtk_runtime_test.sh
run d1-config-files   sh tools/config_test.sh
run d1-sensors        sh tools/sensor_test.sh
run d2-flash-log      sh tools/d2_host_test.sh
run d3-timing         sh tools/cody_test.sh
run d4-ml             sh tools/ml_test.sh
run d5-bench          sh tools/d5_test.sh
run d6-display        sh tools/d6_test.sh
run d6-live           sh tools/d6_live_test.sh
# Each Python test file runs in its own process (one of them replaces the serial
# module). test_application_smoke, test_window_report, test_mtk_check and
# test_d5_bench run inside the D3, D6 and D5 suites above.
for t in test_ml_loader test_mtk_monitor test_sensor_loader test_sonar_fixture test_docs; do
  run "py-$t" "$PY" -m unittest discover -s tests -p "$t.py"
done
if [ "${BUILD:-0}" = 1 ]; then
  run firmware-build  env BOARD=n16r8 sh tools/mtk.sh build
  run firmware-image  sh tools/mtk.sh image esp32s3_mtk/build-n16r8/esp32s3_mtk.elf
fi
echo "$pass passed, $fail failed" | tee -a "$OUT/summary.txt"
echo "Logs: $OUT"
[ "$fail" -eq 0 ]
