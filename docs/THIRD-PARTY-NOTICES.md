# Third-party notices

Gridline F1 is an unofficial personal project. Formula 1 and F1 are trademarks of
Formula One Licensing B.V. Nothing here is affiliated with or endorsed by Formula 1.

## Data
- **Jolpica-F1 API** (https://api.jolpi.ca, https://github.com/jolpica/jolpica-f1) - data licensed
  CC BY-NC-SA 4.0; free for non-commercial use. Test fixtures in `test/fixtures/jolpica/` are copied
  from the jolpica-f1 repository (Apache License 2.0 for the code; see `LICENSE-jolpica.txt`).

## Firmware components
| Component | Version | License |
|---|---|---|
| arduino-esp32 (Espressif) | 2.0.17 | LGPL-2.1-or-later (ESP-IDF components Apache-2.0) |
| WiFiClientSecure from arduino-esp32 2.0.17, **modified** (`lib/WiFiClientSecure`, see PATCHED.md) | 2.0.17 + patch | as original (esp_crt_bundle.c: Apache-2.0) |
| ESP-IDF qrcode component (Nayuki qrcodegen) | in arduino-esp32 2.0.17 | MIT |
| TFT_eSPI (Bodmer) | 2.5.43 | FreeBSD / MIT (see library license) |
| ArduinoJson (Benoit Blanchon) | 7.4.2 | MIT |
| Barlow Semi Condensed font (The Barlow Project Authors) | Google Fonts | SIL Open Font License 1.1 (`tools/fonts/OFL.txt`) |
| Mozilla CA root store via certifi | 2026.10.18 | MPL-2.0 |

## Installer components (bundled wheels, unmodified)
| Package | Version | License |
|---|---|---|
| esptool (Espressif) | 4.8.1 | GPL-2.0-or-later |
| pyserial | 3.5 | BSD-3-Clause |
| cryptography | 43.0.3 | Apache-2.0 OR BSD-3-Clause |
| cffi / pycparser | 1.17.1 / 2.22 | MIT / BSD-3-Clause |
| bitstring / bitarray | 4.2.3 / 2.9.3 | MIT / PSF |
| ecdsa / six | 0.19.0 / 1.16.0 | MIT |
| reedsolo | 1.7.0 | Public Domain / MIT-0 |
| PyYAML | 6.0.2 | MIT |
| intelhex | 2.3.0 | BSD-3-Clause |
| argcomplete | 3.5.1 | Apache-2.0 |

Each wheel contains its own license file. Source code for esptool: https://github.com/espressif/esptool

## Development/test only (not in the firmware)
- Nayuki QR Code generator (`test/sim/third_party`, MIT) for the host simulator.
- Espressif QEMU (GPL-2.0) used for emulator tests; not distributed.
