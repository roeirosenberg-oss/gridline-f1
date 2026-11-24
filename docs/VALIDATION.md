# Test report – Gridline F1 version 1.2.0

Date: 24 October 2026. The full logs are in the `docs/validation-logs/` folder.

> **Honest bottom line:** the firmware was built, tested thoroughly on the PC and in an emulator, and tested by running the real binary in an ESP32 emulator. **At the time of this report it had not yet been tested on the physical board.** A successful build and emulator tests are not proof that the screen, touch, Wi‑Fi or TLS work on the board – that is the next step. (Later on-board results are listed in the version sections at the end.)

---

## Part 1 – Tests run on the development machine

### 1.1 Build and link
| Test | Result |
|---|---|
| Build `st7789` (PlatformIO, espressif32 6.9.0, arduino‑esp32 2.0.17) | ✅ Succeeded, 0 warnings in project code with `-Wall -Wextra` |
| Build `ili9341` | ✅ Succeeded |
| Fit in the partitions | ✅ App 1,376,080 bytes = 43.7% of the 3MB partition; static RAM 75.5KB of 320KB |
| Check of the merged file | ✅ Bootloader at `0x1000`, partition table at `0x8000` identical to the CSV, app at `0x10000`; header: DIO, 40MHz, 4MB; chip ID = classic ESP32 |

Note: the PlatformIO registry (api.registry.platformio.org) was blocked in the development environment, so the toolchain packages were installed manually from the same official sources on GitHub (crosstool‑NG esp‑2021r2‑patch5, arduino‑esp32 2.0.17, esptool 4.5.1, platform‑espressif32 v6.9.0). `platformio.ini` itself is standard and builds normally on a computer with registry access.

### 1.2 Unit tests on the PC – 42,664 tests, 0 failures
Run with AddressSanitizer and UndefinedBehaviorSanitizer (`tools/run_host_tests.sh`).

| Area | What was tested |
|---|---|
| Israel time and DST | Every clock change 2013–2045 against tzdata 2026e (Python zoneinfo) – 33 years, 29,712 sample points, including the repeated hour at the end of DST. Including the change on **the next day**: 25.10.2026 at 02:00 (23:00 UTC) |
| Date parsing | Invalid dates, leap years, times with/without `Z`, fractional seconds, offsets |
| HTTP reader | Content‑Length, chunked (including extensions and trailers), 100 responses, 429 with Retry‑After, malformed/long/too many headers, unsupported gzip, connection closed midway, timeout, oversized body (rejected before reading) |
| JSON parsing – real responses | Season calendar, driver standings, constructor standings, race results (see the note on where the files come from below) |
| Edge cases | `null` fields, missing fields, numbers instead of strings, decimal points, DNF/DSQ/DNS, empty lists, results not yet published, mismatched season, duplicate rounds |
| Malformed input | Empty JSON, an HTML error page, truncated JSON, deep nesting, a 400-race response (rejected by the 48KB memory budget) |
| Truncation | **2,609 truncated versions** of the real responses – none was accepted as valid |
| Fuzzing | 4,000 random mutations – no crash or memory error |
| Full pipeline | HTTP → parsing, across 30 different packet splits, chunked and plain |
| Cache | Save/load, detection of every bit flip, truncated file, wrong dataset |
| Retries | Increasing backoff, 30-minute cap, jitter, Retry‑After, 429 |
| Scheduling | First boot, fresh cache, season rollover, race weekend (refresh every 10 minutes), global block after a network failure |
| "Next race" | Before the season, session day, live race, after a race, end of season, next season, race without a time |
| Touch calibration | Recovering a rotated/flipped mapping with noise, rejecting points on a straight line |

### 1.3 UI simulator – 26 screenshots
The **real** UI code (`ui*.cpp`, `display.cpp`) was compiled against a framebuffer implementation of TFT_eSPI that mimics the library's font placement. The screenshots are in `preview/screens`. The simulator found these issues, which were fixed: wrong color blending, a dialog that didn't close on screen change, truncated dialog text, a clipped column header and drawing leftovers at the bottom of lists. The QR code on the setup screen was decoded successfully (OpenCV) to `WIFI:T:WPA;S:…;P:…;;`.

### 1.4 ESP32 emulator (Espressif's QEMU) – the compiled binary itself
| Scenario | Result |
|---|---|
| A – first boot, empty flash | ✅ Bootloader (DIO), boot, `PSRAM none`, LittleFS format, display init, touch calibration screen starts |
| B – normal boot (NVS with sample calibration and network) | ✅ Calibration loaded, Wi‑Fi credentials detected, UI and network task started. ⚠️ Stops inside ESP‑IDF's radio init (`register_chipv7_phy`) – a **known QEMU limitation**, since it doesn't emulate Wi‑Fi |
| C1 – test build (not included in the package) without radio | ✅ Parsing the real responses **on the Xtensa CPU** through the HTTP reader, rejecting truncated JSON and an HTML page, not overwriting good data, atomic cache writes, Israel time, UI loop running with stable memory (about 256KB free without Wi‑Fi) |
| C2 – second boot on the same flash | ✅ Cache restored from LittleFS |
| In all scenarios | ✅ The Wi‑Fi password did not appear in the log |

### 1.5 Windows installer
| Test | Result |
|---|---|
| Offline Python packages | ✅ 13 wheel files for Windows x64 / Python 3.12 with pinned hashes; dependency resolution for Windows was tested (a bitstring/bitarray conflict was found and fixed) |
| Firmware file verification | ✅ SHA‑256, size, headers, addresses, match between the merged file and its parts |
| A modified file (one bit) | ✅ Rejected |
| Chip and flash detection from esptool output | ✅ ESP32‑D0WD‑V3/D0WDQ6/U4WDH accepted, S3/C3 rejected, 4MB detected (a detection bug was found and fixed) |
| CH340 port detection | ✅ Including a changed COM number and other ports on the computer |
| Install flow (dry run) | ✅ flash_id → 4MB backup → erase → write at `0x0` with DIO/40m/4MB; without a backup `SKIP` is required; any other input cancels |
| ZIP | ✅ CRC check for every file + SHA‑256 comparison of every file against `SHA256SUMS.txt` |

### 1.6 Data source – what was and wasn't tested
- **Tested:** Jolpica's official documentation (endpoints, rate limits of 4/second and 500/hour, User‑Agent requirement, maximum limit of 100) and their server code (how `current`/`next`/`last` are computed, when `position` is `null`, `positionText` values R/D/W/F, decimal points). As a result, the "next race" is computed on the device from the clock rather than from the server's `next`.
- **Test files:** real JSON responses from the compatibility test suite in the jolpica-f1 repository on GitHub (the commit is recorded in `test/fixtures/jolpica/SOURCE_COMMIT.txt`). These are Ergast/Jolpica-format responses from the 2023 season. Responses written to match the current server format (2025 onward) were also tested.
- **Not tested:** a live call to `api.jolpi.ca` and a check of its real certificate chain – access to that domain was blocked in the development environment. The chain is printed to the log on first boot on the board.

---

## Part 2 – Tests run on the physical ESP32

**None yet at the time of this report.** The board was not connected to the development environment, and nothing was written to it. (See the version sections below for the later on-board results.)

---

## Part 3 – Tests that still require the physical board

| # | Test | How to test |
|---|---|---|
| 1 | The install itself (backup, erase, write) | `install.bat` |
| 2 | The screen shows a picture with correct colors (ST7789, BGR, no inversion) | Splash and calibration screens – red is red |
| 3 | Backlight and brightness changes | Settings → Brightness |
| 4 | Touch: calibration, accuracy, no double taps | 5 targets + check; navigating between tabs |
| 5 | Wi‑Fi setup mode: temporary network, setup page, QR, connecting and saving | The phone |
| 6 | Wi‑Fi connection and clock sync | Status `Syncing clock` → `Updated`; About → Clock |
| 7 | Real TLS to api.jolpi.ca, including the chain in the log | `serial-monitor.bat` – `TLS cert[...]` lines |
| 8 | TLS self-test (wrong name / self-signed / unknown root / expired are rejected) | About → Run TLS self-test, `4/4 checks passed` |
| 9 | Real 2026 data on all five screens | Compare with the F1 website |
| 10 | Israel time after the switch to winter time (25.10.2026) | Time of the next race |
| 11 | Working without internet: router unplugged → `Offline` + cached data, automatic recovery | Unplug/replug the router |
| 12 | Restart: cached data shown immediately as `Cached` | Power cut |
| 13 | BOOT button (3 seconds) opens Wi‑Fi setup | Long press |
| 14 | Night dimming | Settings → Night dimming |
| 15 | Memory and long-term stability with Wi‑Fi and TLS | About → Memory, and the `MEM` log every 10 minutes |
| 16 | ILI9341 version | Only if ST7789 shows no picture |

---

## Known limitations
- 2.4GHz Wi‑Fi only (a hardware limitation).
- If a network blocks NTP, the clock won't sync and there will be no updates (on purpose – certificates are not checked against an unverified clock).
- A future change to Israel's DST law will require a firmware update.
- Team names/colors: team colors are fixed in code; an unknown team is shown in gray with the name from the API.
- The API is a volunteer project with no availability guarantee; if it is down, the cached data is shown.

---

## Changes in version 1.0.1 (24.10.2026, after the first test on the board)
- **Test on the physical board:** the install succeeded (`Hash of data verified`), the display and touch worked up to the Wi‑Fi setup screen, and the setup page opened on the phone. Connecting to the network failed with `Network not found`.
- **Fix:** Wi‑Fi setup now waits for a background scan to finish before trying to connect, tries up to 4 times before declaring "not found", uses the exact name (including upper/lower case) and the channel from the scan results, and shows the list of 2.4GHz networks the board can see. The scan is also printed to the log.
- The automated tests (build, unit tests, simulator, emulator, installer) were run again on this version. Wi‑Fi setup mode can't be tested in the emulator (no radio), so it was tested only on the board.

## Changes in version 1.0.2 (24.10.2026) – fix for verifying the server's certificate
- **Test on the physical board, version 1.0.1:** Wi‑Fi connected (the real network name had a trailing space, which explained the failure in version 1.0.0), RSSI -53 dBm, NTP synced (18:34 Israel time, UTC+3 – correct), 191KB of free memory. The first HTTPS request was rejected: `esp_crt_verify_callback(): Failed to verify certificate`, mbedTLS `-0x3000`.
- **Diagnosis:** the server runs on AWS. AWS chains (and Google's too) end with a "cross-signed" copy of a trusted root certificate (for example Starfield Services Root G2) issued by an old root that Mozilla removed (Starfield Class 2). The bundle verification code in arduino‑esp32 2.0.17 only checks the issuer of the topmost certificate sent, so it rejected a valid chain. Browsers and newer ESP‑IDF versions handle this.
- **Fix:** a local copy of the WiFiClientSecure library (`lib/WiFiClientSecure`, see `PATCHED.md`) with an extended verification rule: the top certificate is accepted if its issuer is a trusted root (the original rule), or if it is itself identical to a trusted root (same name **and same public key**), or if a certificate lower in the chain is such a root. In addition, the validity check stops at the trusted root (as in browsers), and on failure the subject and issuer of the top of the chain are logged.
- **Tested on the PC against real mbedTLS 2.28** (`test/tls/run_tls_tests.sh`, with a certificate hierarchy generated for the test): the **original** code rejects an AWS-style chain – reproducing exactly the failure on the board; the fixed code accepts it, and rejects: a wrong server name, a certificate with the same name but a foreign key (an attack), an untrusted chain, and a self-signed certificate. All 117 root certificates in the store are recognized correctly. All other tests (unit, simulator, emulator, installer) were run again.
- **Still required the board:** a real HTTPS connection to api.jolpi.ca with the fix (the log shows the full chain and `trusted root reached`).
- **Confirmed on the physical board (24.10.2026 19:25), version 1.0.2:** the display works and shows real data – verified HTTPS connection to api.jolpi.ca, downloading and parsing work on the board.

## Version 1.1.0 (28.10.2026) – Portrait mode
**What's new:** a portrait (240×320) firmware build alongside the landscape build, and a Settings → Orientation option to switch between four orientations without a computer. All screens were redesigned for portrait: a two-row header with a settings button, a 2×2 countdown, ten rows in tables with paging at the bottom, and a portrait Wi‑Fi setup screen. Touch calibration is stored in a single coordinate system and is valid in all orientations.

**Tested on the PC (version 1.1.0):**
- Build of 4 versions (ST7789/ILI9341 × landscape/portrait) – no warnings, 44% of the partition.
- Unit tests: 49,122 tests, 0 failures. Including coordinate conversion for all 4 rotations, and a calibration made in portrait and also checked in landscape.
- Simulator: 52 screenshots (26 per orientation) from the real UI code. The QR codes on both setup screens were decoded successfully.
- **Touch tests in the simulator** through the real touch handler: tabs, paging, the settings button in the header, and switching orientation ×3 (portrait → landscape 180 → portrait 180 → landscape) – all passed.
- Emulator: the portrait build boots at rotation 0 and the landscape build at rotation 1, including loading calibration and memory.
- Installer: the 4-option menu was tested.
- All TLS, parsing and cache tests were run again.

**Requires the physical board:** portrait display on the real screen. Touch accuracy in portrait with the existing calibration – if it's not accurate, recalibrating in portrait always fixes it. Which rotation (0 or 2) matches how the board stands.

## Version 1.2.0 (28.10.2026) – a simple orientation toggle in Settings
- **What changed:** Settings has an **Orientation** switch with two buttons, Wide (landscape) and Tall (portrait). One tap switches immediately, and the screen stays on Settings. Instead of cycling through 4 modes there is a separate **Upside down** row for a 180° flip. This was a user request.
- **Tested in the simulator through the real touch handler:** Wide → landscape; Tall → portrait; tapping Tall when already in portrait changes nothing; Upside down switches to portrait 180; Wide with the flip on gives landscape 180; turning the flip off returns to landscape. All 15 touch tests passed. All other tests were run again.
