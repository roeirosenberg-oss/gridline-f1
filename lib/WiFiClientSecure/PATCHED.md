# Patched WiFiClientSecure (arduino-esp32 2.0.17)

Project-local copy of `libraries/WiFiClientSecure` from arduino-esp32 2.0.17
(Apache-2.0 / LGPL as in the original). PlatformIO uses this copy instead of the
framework one because it lives in the project's `lib/` folder.

Only `src/esp_crt_bundle.c` / `.h` were changed:

* `esp_crt_verify_callback()` accepts the top certificate of a chain when
  1. its issuer is a bundled root whose key verifies its signature (original rule), or
  2. it has exactly the same subject name and public key as a bundled root
     (cross-signed copy of a trusted root, as sent by AWS/ACM and Google servers).
* Linear exact-match lookups instead of a binary search on a name prefix.
* On failure the subject and issuer of the untrusted chain top are logged.
* New helper `gl_crt_bundle_is_anchor()`.

Reason: with the original code `api.jolpi.ca` was rejected on the real board
(`esp_crt_verify_callback(): Failed to verify certificate`) because the chain
ends with a cross-signed root issued by a root that Mozilla has removed.
Tests: `test/tls/run_tls_tests.sh` (host, real mbedTLS 2.28 + generated PKI).
