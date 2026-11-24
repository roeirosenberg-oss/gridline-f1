# Gridline F1 – Install and user guide

Gridline F1 is a new, standalone firmware that turns the ESP32 touch-screen board (ESP32-2432S028) into a desktop Formula 1 display. The board connects to Wi‑Fi on its own, pulls real data from the Jolpica‑F1 API, and runs without a computer once installed.

> Unofficial personal project. Formula 1 and F1 are trademarks of Formula One Licensing B.V.

---

## 1. What you need before you start

| What | Details |
|---|---|
| A Windows computer | With Python 3.12 that runs as `py -3.12` |
| A USB data cable | A charge-only cable will **not** work. The board has both Micro‑USB and USB‑C – either one is fine |
| A 2.4GHz Wi‑Fi network | The ESP32 does **not** support 5GHz. Most home routers also have a 2.4GHz network |
| A phone | To set up the display's Wi‑Fi |
| About 15 minutes | Including a full backup of the existing firmware |

### ⚠️ Important: what gets erased

Gridline F1 is a completely new project with its own memory layout (partition table). Installing it **erases the board's entire memory**: the existing firmware, the Wi‑Fi password saved in it, the touch calibration and all settings. They cannot be carried over – after installing you calibrate the screen again and re-enter the Wi‑Fi (about 2 minutes).

That's why the installer **offers a full backup** (4MB) of what is on the board today. Saying yes is strongly recommended. With the backup you can always go back to exactly the previous state.

---

## 2. Installation – step by step

### Step 1 – Extract the file
1. Right-click the package ZIP (for example `Gridline-F1-v1.2.0-windows.zip`) → **Extract All**.
2. Open the folder that was created (for example `Gridline-F1-v1.2.0`).

> Don't run the files from inside the ZIP without extracting – it won't work.

### Step 2 – Connect the board
1. Connect the board to the computer with a USB cable.
2. (Optional check) Open **Device Manager** → **Ports (COM & LPT)**. You should see `USB-SERIAL CH340 (COMx)`. The COM number can vary – the installer finds it on its own.

### Step 3 – Run the installer
1. Double-click **`install.bat`**.
2. The first time only, you'll see `Preparing the installer` – the installer sets up a private Python environment from the files in the package (no internet needed, about half a minute).

Here is what you will see and how to answer:

| What appears on screen | What to do |
|---|---|
| `What would you like to do?` | Type `1` and press Enter (Install Gridline F1) |
| `Checking firmware files` … `OK` | Automatic check of the files (SHA‑256, size, structure). If `FAILED` appears – download the package again |
| `Choose the display type and orientation` | Type `1` for **landscape** or `2` for **portrait** (both are ST7789 – the right choice for this board). You can change it later in Settings |
| `Found: COM5 (USB-SERIAL CH340)` `Use COM5?` | Press Enter (yes) |
| `Board OK: classic ESP32, 4 MB flash` | The board was detected successfully |
| `Make a full backup … ? (Y/n)` | Press Enter (yes). The backup takes 2–6 minutes and is saved in the `backups` folder |
| `Erase the board and install Gridline F1 now? (Y/n)` | Press Enter (yes) |
| `Erasing the flash`, then `Writing Gridline F1` | Wait. At the end esptool verifies the write (hash) |
| `Open the serial monitor … ? (Y/n)` | Enter opens a log window (optional). To close it: Ctrl+C |

If you chose not to back up, the installer asks you to type `SKIP` in capital letters – anything else cancels without changing anything.

### Step 4 – Calibrate the touch screen (on first boot)
1. The screen shows `TOUCH CALIBRATION` with a target (a cross in a circle).
2. Touch the **center** of each target – preferably with a fingernail or a plastic stylus (it is a resistive, press-type touch screen, not capacitive). There are 5 targets.
3. At the end a check target (`CHECK`) appears. Touch it. If the accuracy is good, `CALIBRATION SAVED` appears.
4. If `NOT ACCURATE ENOUGH` or `CHECK FAILED` appears, the process starts again automatically.

### Step 5 – Set up Wi‑Fi from your phone
After calibration the display switches to the `WI-FI SETUP` screen and shows:
- A temporary network name, for example `GRIDLINE-7F3A`
- A readable temporary password (without confusing characters like 0/O or 1/l), for example `mx7k-p4ra`
- A QR code

1. On the phone: **Settings → Wi‑Fi** → join the `GRIDLINE-....` network with the password on the screen (or scan the QR code with the camera).
2. The setup page usually opens by itself. If not, open a browser and go to **`http://192.168.4.1`**.
3. Pick your home network from the list (or type its name), enter the Wi‑Fi password and tap **Connect**.
4. If everything is fine, the phone shows `Connected!` and the display shows `Connected to … - restarting`. The display restarts and begins downloading data.
5. If `Wrong password?` or `Network not found (is it 2.4 GHz?)` appears, fix it and try again. The phone may drop off the GRIDLINE network during the check; just join it again.

**Your home Wi‑Fi password is stored only in the board's memory and is never written to the logs.**

### Step 6 – That's it!
From here on the display runs on its own. You can power it from a regular USB phone charger.

---

## 3. Everyday use

### Landscape or portrait
There are two firmware versions, and both include exactly the same screens:
- **Landscape (320×240):** the tab bar is at the bottom, and settings are under the ☰ icon in the bottom-right corner.
- **Portrait (240×320):** the header has two rows (screen name and the ☰ settings button on top, status and time below). The countdown is a large 2×2 grid, and tables show 10 rows per page with paging arrows at the bottom.

You can switch between orientations at any time without a computer: **Settings → Orientation → Wide / Tall** (one tap). If the picture is upside down relative to how the board stands, turn on **Upside down**.


The navigation bar at the bottom of the screen:

| Tab | What it shows |
|---|---|
| **NEXT** | The next race: name, circuit, country, countdown, race date and start time **in Israel time**, and the next session. Tapping the countdown area shows the full weekend schedule |
| **DRIVERS** | Driver standings (position, code, name, team, points). Tapping a row offers to mark that driver as your favorite |
| **TEAMS** | Constructor standings with a relative points bar |
| **RESULTS** | Last race results, including times and retirements (DNF/DSQ/DNS) |
| **CALENDAR** | The season calendar. The next race is marked `NEXT`, finished races with ✓. Tapping a race shows all its sessions |
| ☰ (right-hand icon; in portrait – top right) | Settings |

The ▲ ▼ arrows on the right switch pages.

### What the status messages mean (top corner)

| Message | Meaning |
|---|---|
| `Updated 16:05` (green dot) | The data was downloaded from the internet at this time |
| `Cached · 16:05` (orange) | Saved data from memory is shown (for example after a restart) – not updated yet |
| `Offline · 16:05` (red) | No Wi‑Fi; the last saved data is shown |
| `Connecting` / `Syncing clock` / `Updating` | Connecting / syncing the clock / downloading data |
| `TLS error · …` | The secure connection failed (see Troubleshooting) |
| `Rate limited` | The server asked to slow down; the display will retry automatically later |
| `No data yet` | The data hasn't been published yet (for example before the season starts) |

The display **never replaces good data with bad data**: if a download fails, the previous data stays and is marked accordingly.

### How often the display updates
- Between race weekends: the season calendar every 6 hours, standings and results every 3–6 hours.
- On a race weekend: every hour, and after the race every 10 minutes until the results are published.
- After a failure: gradual retries (30 seconds, 1 minute, 2, 5, 10… up to 30 minutes).
This is far below the server's limits (500 requests per hour).

### Between seasons
After the last race, `SEASON COMPLETE` is shown with the champion (once the final standings are available) and a note that next season's calendar hasn't been published yet. As soon as it is published, the next race appears automatically. At the start of a year, until there is data for the new season, the final standings of the previous season are shown (the year appears in the title).

---

## 4. Settings (☰)

| Setting | Description |
|---|---|
| **Brightness** | Screen brightness (– / +) |
| **Orientation** | Screen orientation: **Wide** = landscape, **Tall** = portrait. One tap switches immediately, and the setting is saved. The touch calibration stays valid |
| **Upside down** | Rotates the picture 180° – for when the board stands upside down |
| **Night dimming** | Automatic dimming at night by Israel time: start time, end time and night brightness level |
| **Accent** | Accent color (6 options) |
| **Favorite driver** | Favorite driver – highlighted in the tables and on the NEXT screen |
| **Wi-Fi** | Opens the Wi‑Fi setup screen again (for example when you replace your router). The current network is kept until you choose a new one |
| **Touch calibration** | Recalibrate the touch screen |
| **Refresh data now** | Refresh immediately (at most once a minute) |
| **About & diagnostics** | Version, Wi‑Fi, clock, the status of each data source, memory, and a TLS self-test |
| **Restart display** | Restart |

Settings are stored in the board's memory and survive power-off. They are written to memory only when something changed (a few seconds after the last change) so the memory doesn't wear out.

### Shortcuts without a computer
- **Open Wi‑Fi setup:** long-press (3 seconds) the **BOOT** button on the board while the display is running. When `Release to open Wi-Fi setup` appears, let go.
- **Recalibrate touch when touch doesn't respond:** touch and hold the screen while it boots (about 3 seconds, until `Keep touching to recalibrate` appears).

---

## 5. Troubleshooting

| Problem | What to do |
|---|---|
| `install.bat` says `py` was not found | Install Python 3.12 from python.org and tick "Add python.exe to PATH" |
| `No USB-SERIAL CH340 device was found` | 1) Try another cable (a data cable). 2) Try the board's other port (Micro‑USB), or USB‑C with a USB‑A to C cable. 3) If there is no CH340 in Device Manager, install WCH's CH340 driver (CH341SER) and restart |
| `Failed to connect` / the install is stuck at `Connecting...` | Hold the **BOOT** button, press and release **RST**, release BOOT, and run again. The installer also retries automatically at a lower speed |
| The screen is black after installing | Open `serial-monitor.bat` and check that `BOOT` lines appear. If there are logs but the screen is dark, try the ILI9341 version (see section 6) |
| Inverted/strange colors | The display settings need tuning – open an issue on GitHub with a photo of the screen |
| Touch isn't accurate | Settings → Touch calibration, or hold the screen during boot |
| `Network not found` in Wi‑Fi setup | Make sure it is a 2.4GHz network and that the board is close to the router |
| Persistent `TLS error` | Check on the About screen that the clock is `synced`. If the clock is fine and the error persists, save a log from `serial-monitor.bat` and open an issue on GitHub |
| `Rate limited` | Nothing to do – it retries automatically |

**Logs:** `serial-monitor.bat` shows the board's log (115200 baud) and saves it in the `logs` folder. The log contains no passwords.

---

## 6. More installer options

| Menu option | When |
|---|---|
| `2) Backup only` | Save a backup without changing anything |
| `3) Restore a backup` | Go back to the firmware that was on the board before the install (from the `backups` folder) |
| `4) Serial monitor` | View the log |
| `5) Update application only` | Update to a future version of Gridline F1 **without** erasing Wi‑Fi/calibration/settings. Works only on a board that already runs Gridline F1 – the installer checks this itself |

**ILI9341 version:** at the display type step, type `3` (landscape) or `4` (portrait). It is meant for boards with an ILI9341 controller. It has not been tested on a real board – use it only if the ST7789 version shows no picture.

**Check files only:** `verify-files.bat` checks the firmware files (SHA‑256, size, structure) without touching the board.

### Firmware files (for advanced manual use)

| File | Address | When |
|---|---|---|
| `ST7789_FULL-INSTALL_merged_write-at-0x0.bin` | `0x0` | Full install (after erasing), landscape |
| `ST7789_PORTRAIT_FULL-INSTALL_merged_write-at-0x0.bin` | `0x0` | Full install, portrait |
| `ST7789_APP-ONLY_update_write-at-0x10000.bin` | `0x10000` | App-only update, without erasing |
| `ST7789_part_bootloader_at-0x1000.bin` | `0x1000` | Part of the merged file |
| `ST7789_part_partition-table_at-0x8000.bin` | `0x8000` | Part of the merged file |

Flash settings: DIO, 40MHz, 4MB. Checksums: `SHA256SUMS.txt`.

---

## 7. Privacy and security (in short)
- All communication with the data server is encrypted with HTTPS, with certificate chain and server name checks, plus a certificate validity check against a clock synced from the network. There is no fallback to insecure HTTP.
- The display doesn't start a secure connection before the clock is synced.
- The data comes from the Jolpica‑F1 API (free, no key, licensed CC BY‑NC‑SA 4.0). There is no middle server.
- Setup mode (Access Point) is protected by a new random password each time, and is active only during setup.
