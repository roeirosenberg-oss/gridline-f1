#!/usr/bin/env bash
# Runs every check that is possible without the physical board and stores the
# logs in docs/validation-logs/. Usage: tools/run_all_checks.sh <qemu-system-xtensa>
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOGS="$ROOT/docs/validation-logs"
QEMU="${1:-}"
mkdir -p "$LOGS"
cd "$ROOT"
status=0
step() { echo; echo "######## $1"; }

step "1. firmware builds (st7789, st7789_portrait, ili9341, ili9341_portrait)"
for e in st7789 st7789_portrait ili9341 ili9341_portrait; do
  pio run -e "$e" 2>&1 | grep -E "RAM:|Flash:|SUCCESS|FAILED|warning|error" | tee "$LOGS/build-$e.txt"
  grep -q SUCCESS "$LOGS/build-$e.txt" || status=1
done

step "2. host unit tests (ASan + UBSan)"
./tools/run_host_tests.sh 2>&1 | grep -v "^/usr\|^In file\|^\s" | tee "$LOGS/host-tests.txt"
grep -q " 0 failures" "$LOGS/host-tests.txt" || status=1

step "3. UI simulator (real UI code, framebuffer mock)"
./tools/run_simulator.sh 2>&1 | tee "$LOGS/simulator.txt"
grep -qi "missing glyph" "$LOGS/simulator.txt" && status=1

step "4. installer logic tests"
python3 test/installer/test_flasher.py 2>&1 | tee "$LOGS/installer-tests.txt"
grep -q "failures: 0" "$LOGS/installer-tests.txt" || status=1

step "4b. TLS certificate-chain verification (real mbedTLS 2.28, original vs patched callback)"
./test/tls/run_tls_tests.sh 2>&1 | grep -v "^\s*\[" | tee "$LOGS/tls-chain-tests.txt"
grep -q "all 11 expectations met" "$LOGS/tls-chain-tests.txt" || status=1
grep -q "117 of 117" "$LOGS/tls-chain-tests.txt" || status=1

if [ -n "$QEMU" ]; then
  step "5. ESP32 emulator (QEMU) boot tests of the compiled binaries"
  pio run -e qemu_selftest >/dev/null 2>&1
  for e in st7789 st7789_portrait qemu_selftest; do
    python3 -m esptool --chip esp32 merge_bin -o ".hostbuild/$e-merged.bin" --flash_mode dio --flash_freq 40m \
      --flash_size 4MB 0x1000 ".pio/build/$e/bootloader.bin" 0x8000 ".pio/build/$e/partitions.bin" \
      0x10000 ".pio/build/$e/firmware.bin" >/dev/null
  done
  (cd .hostbuild && python3 ../tools/qemu_boot_test.py "$QEMU" st7789-merged.bin qemu_selftest-merged.bin st7789_portrait-merged.bin) 2>&1 | tee "$LOGS/qemu-tests.txt"
  grep -q "failures: 0" "$LOGS/qemu-tests.txt" || status=1
  cp .hostbuild/qemu-*.log "$LOGS/" 2>/dev/null
  sha256sum .pio/build/st7789/firmware.bin | tee "$LOGS/qemu-tested-st7789-app.sha256"
fi
echo
echo "overall status: $status"
exit $status
