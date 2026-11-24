# Updating the security certificates (CA) in the future

The firmware contains a store of public root certificates (Root CAs) that it uses to verify that it is really talking to `api.jolpi.ca`.

- Source: Mozilla's certificate store, as packaged in the Python `certifi` package.
- Current version: `certifi 2026.10.18`, 117 certificates (shown on the About → TLS trust screen).
- Format: the ESP‑IDF bundle (`x509_crt_bundle`), loaded with `WiFiClientSecure::setCACertBundle()`.
- Generated file: `src/certs/ca_bundle.h`.

## When does it need updating?
- Root certificates change slowly (over years). The full Mozilla store also covers the server moving to a different certificate authority.
- If a persistent `TLS error` appears one day, the clock on the About screen is synced, and the log (`serial-monitor.bat`) shows a certificate verification error – it is probably time to update.

## How to update (on Windows)
1. Download the source code (`source/gridline-f1-source-v1.2.0.zip`) and extract it.
2. In the project folder:
   ```
   py -3.12 -m pip install --upgrade certifi cryptography platformio==6.1.18
   py -3.12 tools\gen_ca_bundle.py
   py -3.12 -m platformio run -e st7789
   ```
3. The script prints how many certificates were included and checks that no development-environment "proxy" certificates got in.
4. Install with `install.bat` (or option 5 – app-only update, which keeps Wi‑Fi and settings). For that, build a package with `tools/make_release.py`.

## Why not "pin" a single certificate?
Pinning the server's specific certificate would break on every certificate renewal (every few months). A full root store + server name check + validity check against the NTP clock gives good security and long-term stability.
