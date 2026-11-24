#!/usr/bin/env bash
# Verifies the patched certificate-bundle callback against real mbedTLS 2.28
# (apt: libmbedtls-dev) and shows that the original arduino-esp32 callback
# rejects the AWS-style cross-signed chain. Also checks that every real root
# in the firmware bundle is recognised as an anchor.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/../.."
OUT="$ROOT/.hostbuild/tls"; mkdir -p "$OUT"
ORIG=/root/.platformio/packages/framework-arduinoespressif32/libraries/WiFiClientSecure/src
python3 "$HERE/gen_pki.py" "$OUT"
build() { gcc -O1 -g -Wall -Wno-unused-function -Wno-unused-result -I"$HERE/shim" -I"$2" "$HERE/test_bundle.c" "$2/esp_crt_bundle.c" -lmbedtls -lmbedx509 -lmbedcrypto -o "$OUT/$1"; }
build patched "$ROOT/lib/WiFiClientSecure/src"
sed 's/^static int esp_crt_verify_callback(/int esp_crt_verify_callback(/;s/^int esp_crt_verify_callback(void \*buf, mbedtls_x509_crt \*crt, int data, uint32_t \*flags);//' "$ORIG/esp_crt_bundle.c" > "$OUT/esp_crt_bundle.c"
cp "$ORIG/esp_crt_bundle.h" "$OUT/"
gcc -O1 -g -w -I"$HERE/shim" -I"$OUT" "$HERE/test_bundle.c" "$OUT/esp_crt_bundle.c" -lmbedtls -lmbedx509 -lmbedcrypto -o "$OUT/original"
echo "== ORIGINAL arduino-esp32 2.0.17 callback"; "$OUT/original" "$OUT" "$OUT/bundle.bin" 2>&1 | grep -v "^\s*\["
echo "== PATCHED Gridline callback"; "$OUT/patched" "$OUT" "$OUT/bundle.bin" 2>&1 | grep -v "^\s*\[" | tee "$OUT/patched.txt"
expect=$'aws_style_cross            cn=api.example.test     -> ACCEPT
aws_style_cross_plus_old   cn=api.example.test     -> ACCEPT
plain_chain                cn=api.example.test     -> ACCEPT
ecdsa_chain                cn=api.example.test     -> ACCEPT
plain_chain                cn=wrong.example.test   -> REJECT
aws_style_cross            cn=wrong.example.test   -> REJECT
evil_same_name_other_key   cn=api.example.test     -> REJECT
untrusted_root_only        cn=api.example.test     -> REJECT
self_signed                cn=api.example.test     -> REJECT
evil_plus_old_root         cn=api.example.test     -> REJECT
aws_style_cross_plus_old   cn=wrong.example.test   -> REJECT'
diff <(echo "$expect") "$OUT/patched.txt" && echo "PATCHED: all 11 expectations met"
# real bundle: every certifi root must be recognised as an anchor by the patched code
python3 - "$ROOT" "$OUT" <<'PY'
import re, sys, certifi
root, out = sys.argv[1], sys.argv[2]
h = open(f"{root}/src/certs/ca_bundle.h").read()
data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", h.split("GL_CA_BUNDLE[] = {")[1]))
open(f"{out}/real_bundle.bin", "wb").write(data)
open(f"{out}/roots.pem", "w").write(open(certifi.where()).read())
PY
cat > "$OUT/anchor_check.c" <<'C'
#include <stdio.h>
#include <stdlib.h>
#include <mbedtls/x509_crt.h>
#include "esp_err.h"
#include "esp_crt_bundle.h"
int main(int c, char **v) {
  FILE *f = fopen(v[1], "rb"); fseek(f, 0, 2); long n = ftell(f); fseek(f, 0, 0);
  unsigned char *b = malloc(n); fread(b, 1, n, f); fclose(f); arduino_esp_crt_bundle_set(b);
  mbedtls_x509_crt roots; mbedtls_x509_crt_init(&roots);
  int bad = mbedtls_x509_crt_parse_file(&roots, v[2]);
  int total = 0, ok = 0;
  for (mbedtls_x509_crt *r = &roots; r && r->raw.len; r = r->next) { total++; ok += gl_crt_bundle_is_anchor(r); }
  printf("real bundle: %d of %d certifi roots recognised as anchors (%d not parsed by mbedTLS)\n", ok, total, bad);
  return ok == total ? 0 : 1;
}
C
gcc -O1 -w -I"$HERE/shim" -I"$ROOT/lib/WiFiClientSecure/src" "$OUT/anchor_check.c" "$ROOT/lib/WiFiClientSecure/src/esp_crt_bundle.c" -lmbedtls -lmbedx509 -lmbedcrypto -o "$OUT/anchor_check"
"$OUT/anchor_check" "$OUT/real_bundle.bin" "$OUT/roots.pem"
