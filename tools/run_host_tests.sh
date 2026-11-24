#!/usr/bin/env bash
# Builds and runs the host unit tests for lib/gridline_core with
# AddressSanitizer + UndefinedBehaviorSanitizer.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/.hostbuild"
AJ_TAG="v7.4.2"   # keep in sync with platformio.ini
mkdir -p "$OUT"
if [ ! -d "$OUT/ArduinoJson" ]; then
  git clone -q --depth 1 --branch "$AJ_TAG" https://github.com/bblanchon/ArduinoJson.git "$OUT/ArduinoJson"
fi
python3 "$ROOT/test/host/gen_tz_expectations.py" "$OUT/tz_expected.csv"
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-maybe-uninitialized \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  -I"$ROOT/lib/gridline_core/src" -I"$OUT/ArduinoJson/src" \
  "$ROOT"/lib/gridline_core/src/*.cpp "$ROOT/test/host/test_main.cpp" \
  -o "$OUT/gridline_tests"
"$OUT/gridline_tests" "$ROOT/test/fixtures/jolpica" "$OUT/tz_expected.csv"
