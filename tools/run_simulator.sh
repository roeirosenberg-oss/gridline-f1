#!/usr/bin/env bash
# Renders the firmware UI on the host (framebuffer mock of TFT_eSPI) and
# writes PNG screenshots to preview/screens/. Requires g++, python3 + Pillow.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/.hostbuild/sim"
SHOTS="$ROOT/preview/screens"
mkdir -p "$OUT" "$SHOTS"
if [ ! -d "$ROOT/.hostbuild/ArduinoJson" ]; then
  git clone -q --depth 1 --branch v7.4.2 https://github.com/bblanchon/ArduinoJson.git "$ROOT/.hostbuild/ArduinoJson"
fi
gcc -O1 -c "$ROOT/test/sim/third_party/qrcodegen.c" -o "$OUT/qrcodegen.o"
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function -Wno-maybe-uninitialized \
  -DGL_DISPLAY_PROFILE='"ST7789"' \
  -I"$ROOT/test/sim/mock" -I"$ROOT/src" -I"$ROOT/lib/gridline_core/src" -I"$ROOT/.hostbuild/ArduinoJson/src" \
  "$ROOT/test/sim/sim_main.cpp" "$ROOT/test/sim/mock/mock_tft.cpp" \
  "$ROOT/src/display.cpp" "$ROOT/src/ui.cpp" "$ROOT/src/ui_screens.cpp" "$ROOT/src/ui_settings.cpp" \
  "$ROOT/src/ui_setup.cpp" "$ROOT/src/ui_calibrate.cpp" \
  "$ROOT"/lib/gridline_core/src/*.cpp \
  "$OUT/qrcodegen.o" \
  -o "$OUT/sim"
rm -f "$OUT"/*.ppm
"$OUT/sim" "$ROOT/test/fixtures/jolpica" "$OUT" | tee "$OUT/sim-output.txt"
grep -q "tap tests: 0 failure" "$OUT/sim-output.txt" || { echo "SIMULATOR TAP TESTS FAILED"; exit 1; }
python3 - "$OUT" "$SHOTS" <<'EOF'
import sys, glob, os
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
for f in sorted(glob.glob(os.path.join(src, "*.ppm"))):
    im = Image.open(f)
    name = os.path.splitext(os.path.basename(f))[0]
    im.save(os.path.join(dst, name + ".png"))
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(os.path.join(dst, name + "@2x.png"))
print("screens written to", dst)
EOF
