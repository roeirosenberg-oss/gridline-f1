#!/usr/bin/env python3
"""Gridline F1 - Windows installer / flasher for the ESP32-2432S028.

Start it with install.bat (double-click), or:
    py -3.12 tools\\gridline_flasher.py

What it does:
  * creates a private Python environment from the bundled, hash-checked wheels
    (no internet needed),
  * verifies every firmware file against manifest.json (size, SHA-256, image
    headers, flash offsets),
  * finds the USB-SERIAL CH340 port automatically (the COM number may change),
  * checks that the board is a classic ESP32 with 4 MB flash,
  * offers a full 4 MB backup of the current flash before anything is erased,
  * erases the flash and writes the merged Gridline image at offset 0x0.

Options:
  --dry-run       print the esptool commands instead of running them
  --check-only    only verify the firmware files and exit
  --port COMx     use this port instead of auto-detection
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PKG = os.path.dirname(HERE)
VENV = os.path.join(HERE, ".venv")
FW_DIR = os.path.join(PKG, "firmware")
BACKUP_DIR = os.path.join(PKG, "backups")
LOG_DIR = os.path.join(PKG, "logs")
FLASH_SIZE = 4 * 1024 * 1024
BAUD_FAST = 460800
BAUD_SAFE = 115200

# USB-serial bridges commonly found on ESP32-2432S028 boards
CH340_IDS = {(0x1A86, 0x7523), (0x1A86, 0x7522), (0x1A86, 0x5523), (0x1A86, 0x55D4)}
OTHER_UART_IDS = {(0x10C4, 0xEA60), (0x0403, 0x6001), (0x0403, 0x6015)}

DRY_RUN = False
LOG_FILE = None


# --------------------------------------------------------------------- utils
def say(msg=""):
    print(msg)
    if LOG_FILE:
        LOG_FILE.write(msg + "\n")
        LOG_FILE.flush()


def header(title):
    say("")
    say("=" * 64)
    say("  " + title)
    say("=" * 64)


def ask(prompt, default=None):
    suffix = f" [{default}]" if default else ""
    try:
        ans = input(f"{prompt}{suffix}: ").strip()
    except EOFError:
        ans = ""
    if LOG_FILE:
        LOG_FILE.write(f"{prompt} -> {ans!r}\n")
    return ans or (default or "")


def yes_no(prompt, default_yes=True):
    d = "Y/n" if default_yes else "y/N"
    while True:
        a = ask(f"{prompt} ({d})").lower()
        if not a:
            return default_yes
        if a in ("y", "yes"):
            return True
        if a in ("n", "no"):
            return False
        say("  Please type Y or N.")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


# --------------------------------------------------------------------- python environment
def in_private_env():
    return os.path.normcase(os.path.abspath(sys.prefix)) == os.path.normcase(os.path.abspath(VENV))


def venv_python():
    if os.name == "nt":
        return os.path.join(VENV, "Scripts", "python.exe")
    return os.path.join(VENV, "bin", "python")


def ensure_environment(argv):
    """Create tools/.venv from the bundled wheels and re-run inside it."""
    if in_private_env():
        return
    if sys.version_info < (3, 9):
        say("Python 3.9 or newer is required (you have %s)." % sys.version.split()[0])
        sys.exit(1)
    py = venv_python()
    need_install = not os.path.exists(py)
    if not need_install:
        rc = subprocess.call([py, "-c", "import esptool, serial"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        need_install = rc != 0
    if need_install:
        header("Preparing the installer (one time, about 30 seconds)")
        say("Creating a private Python environment in tools\\.venv ...")
        subprocess.check_call([sys.executable, "-m", "venv", VENV])
        lock = os.path.join(HERE, "requirements-win.lock")
        wheels = os.path.join(HERE, "wheels")
        cmd = [py, "-m", "pip", "install", "--disable-pip-version-check", "--no-index", "--find-links", wheels,
               "--require-hashes", "-r", lock]
        say("Installing esptool and pyserial from the bundled files (no internet needed) ...")
        rc = subprocess.call(cmd)
        if rc != 0:
            say("")
            say("The offline install failed (this can happen on non-64-bit Windows or other Python versions).")
            if yes_no("Try to download esptool 4.8.1 and pyserial 3.5 from the internet instead?", True):
                rc = subprocess.call([py, "-m", "pip", "install", "--disable-pip-version-check", "esptool==4.8.1",
                                      "pyserial==3.5"])
            if rc != 0:
                say("Could not prepare the environment. See docs\\HEBREW-GUIDE.html, section 'Troubleshooting'.")
                sys.exit(1)
    rc = subprocess.call([py, os.path.abspath(__file__)] + argv)
    sys.exit(rc)


# --------------------------------------------------------------------- firmware verification
class FirmwareError(Exception):
    pass


def load_manifest():
    path = os.path.join(FW_DIR, "manifest.json")
    if not os.path.exists(path):
        raise FirmwareError("firmware\\manifest.json is missing - the ZIP was not fully extracted?")
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def verify_image_headers(kind, data, offset):
    """Basic structural checks of ESP32 flash images."""
    if kind == "merged":
        if len(data) < 0x10000 + 0x20:
            raise FirmwareError("merged image is too small")
        if data[0x1000] != 0xE9:
            raise FirmwareError("merged image: no bootloader at 0x1000")
        if data[0x8000:0x8002] != b"\xaa\x50":
            raise FirmwareError("merged image: no partition table at 0x8000")
        if data[0x10000] != 0xE9:
            raise FirmwareError("merged image: no application at 0x10000")
        chip = int.from_bytes(data[0x1000 + 12:0x1000 + 14], "little")
    elif kind == "app":
        if data[0] != 0xE9:
            raise FirmwareError("application image has no ESP image header")
        chip = int.from_bytes(data[12:14], "little")
    elif kind == "bootloader":
        if data[0] != 0xE9:
            raise FirmwareError("bootloader image has no ESP image header")
        chip = int.from_bytes(data[12:14], "little")
    elif kind == "partitions":
        if data[0:2] != b"\xaa\x50":
            raise FirmwareError("partition table has no valid entries")
        return
    else:
        return
    if chip != 0:  # 0 = ESP32 (classic)
        raise FirmwareError(f"{kind} image is built for chip id {chip}, not the classic ESP32")


def verify_variant(manifest, variant):
    v = manifest["variants"].get(variant)
    if not v:
        raise FirmwareError(f"variant {variant} not present in this package")
    blobs = {}
    for kind in ("merged", "app", "bootloader", "partitions"):
        e = v[kind]
        path = os.path.join(FW_DIR, e["file"])
        if not os.path.exists(path):
            raise FirmwareError(f"missing file firmware\\{e['file']}")
        size = os.path.getsize(path)
        if size != e["size"]:
            raise FirmwareError(f"{e['file']}: size {size} does not match manifest ({e['size']})")
        digest = sha256_file(path)
        if digest != e["sha256"]:
            raise FirmwareError(f"{e['file']}: SHA-256 mismatch (file damaged or modified)")
        with open(path, "rb") as f:
            data = f.read()
        verify_image_headers(kind, data, e["offset"])
        blobs[kind] = data
    # offsets must be the standard ESP32 layout used by this firmware
    expected = {"merged": 0x0, "bootloader": 0x1000, "partitions": 0x8000, "app": 0x10000}
    for kind, off in expected.items():
        if v[kind]["offset"] != off:
            raise FirmwareError(f"{kind} offset 0x{v[kind]['offset']:x} is not 0x{off:x}")
    m = blobs["merged"]
    for kind in ("bootloader", "partitions", "app"):
        off = expected[kind]
        part = blobs[kind]
        # the separate files are byte-exact slices of the merged image
        if m[off:off + len(part)] != part:
            raise FirmwareError(f"merged image does not contain the same {kind} as the separate file")
    if len(m) > FLASH_SIZE:
        raise FirmwareError("merged image is larger than 4 MB")
    app_limit = manifest.get("app_partition_size", 0x300000)
    if len(blobs["app"]) > app_limit:
        raise FirmwareError("application does not fit its partition")
    return v


def verify_all(manifest):
    header("Checking firmware files")
    ok = []
    for variant in manifest["variants"]:
        try:
            verify_variant(manifest, variant)
            say(f"  {variant:8s}  OK  (SHA-256, size, image headers, offsets)")
            ok.append(variant)
        except FirmwareError as e:
            say(f"  {variant:8s}  FAILED: {e}")
    return ok


# --------------------------------------------------------------------- firmware choice
VARIANT_MENU = [
    ("st7789", "ST7789  - landscape (wide)   - recommended for your board"),
    ("st7789_portrait", "ST7789  - portrait (tall)    - recommended for your board"),
    ("ili9341", "ILI9341 - landscape (wide)   - only for ILI9341 boards (untested on your unit)"),
    ("ili9341_portrait", "ILI9341 - portrait (tall)    - only for ILI9341 boards (untested on your unit)"),
]


def choose_variant(variants, title):
    """Ask for display controller + orientation. Returns a variant key or None."""
    header(title)
    shown = [(k, label) for k, label in VARIANT_MENU if k in variants]
    for i, (_, label) in enumerate(shown, 1):
        say(f"  {i}) {label}")
    say("  (The orientation can be changed later on the display: Settings -> Orientation.)")
    while True:
        a = ask("Your choice", "1")
        if a.isdigit() and 1 <= int(a) <= len(shown):
            return shown[int(a) - 1][0]
        if a.lower() == "q":
            return None
        say("  Please type one of the numbers above (or Q to quit).")


# --------------------------------------------------------------------- serial ports
def list_candidate_ports():
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    ch340, other = [], []
    for p in ports:
        ids = (p.vid, p.pid)
        desc = (p.description or "") + " " + (p.manufacturer or "")
        if ids in CH340_IDS or "CH340" in desc.upper() or "CH9102" in desc.upper():
            ch340.append(p)
        elif ids in OTHER_UART_IDS:
            other.append(p)
    return ch340, other, ports


def choose_port(forced=None):
    if forced:
        return forced
    while True:
        header("Finding the board")
        ch340, other, allp = list_candidate_ports()
        candidates = ch340 or other
        if len(candidates) == 1:
            p = candidates[0]
            say(f"Found: {p.device}  ({p.description})")
            if yes_no(f"Use {p.device}?", True):
                return p.device
        elif len(candidates) > 1:
            say("Several USB serial devices were found:")
            for i, p in enumerate(candidates, 1):
                say(f"  {i}) {p.device}  {p.description}")
            a = ask("Type the number of the board's port")
            if a.isdigit() and 1 <= int(a) <= len(candidates):
                return candidates[int(a) - 1].device
        else:
            say("No USB-SERIAL CH340 device was found.")
            if allp:
                say("Other ports present: " + ", ".join(f"{p.device} ({p.description})" for p in allp))
            say("")
            say("Checklist:")
            say("  1. Connect the board with a DATA cable (some cables only charge).")
            say("  2. Try the board's Micro-USB port, or USB-C with a USB-A-to-C cable.")
            say("  3. Install the CH340 driver if Windows does not show 'USB-SERIAL CH340'")
            say("     in Device Manager -> Ports (see the guide).")
        a = ask("Press Enter to search again, or type a port name like COM5, or Q to quit")
        if a.lower() == "q":
            sys.exit(0)
        if re.fullmatch(r"(?i)com\d+|/dev/\S+", a):
            return a.upper() if a.lower().startswith("com") else a


# --------------------------------------------------------------------- esptool wrapper
def esptool(port, args, baud=BAUD_FAST, capture=False):
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32", "--port", port, "--baud", str(baud),
           "--before", "default_reset", "--after", "hard_reset"] + args
    say("> esptool " + " ".join(cmd[3:]))
    if DRY_RUN:
        return 0, ""
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
                            encoding="utf-8", errors="replace")
    out = []
    for line in proc.stdout:
        out.append(line)
        sys.stdout.write(line)
        if LOG_FILE:
            LOG_FILE.write(line)
    proc.wait()
    return proc.returncode, "".join(out)


def esptool_retry(port, args, capture=False):
    rc, out = esptool(port, args, BAUD_FAST, capture)
    if rc != 0 and not DRY_RUN:
        say("")
        say("That did not work at high speed - retrying slowly (115200 baud).")
        say("Tip: if it keeps failing, hold the BOOT button, tap RST, release BOOT, then retry.")
        time.sleep(1.5)
        rc, out = esptool(port, args, BAUD_SAFE, capture)
    return rc, out


def is_classic_esp32(esptool_output):
    """True for 'Chip is ESP32-D0WD-V3 (revision v3.1)', 'Chip is ESP32-D0WDQ6', ...;
    False for ESP32-S2/S3/C3/C6/H2/P4 or when no chip line is present."""
    m = re.search(r"Chip is (ESP32\S*)", esptool_output)
    if not m:
        return False
    return not re.match(r"ESP32-(S|C|H|P)\d", m.group(1))


def parse_flash_size(esptool_output):
    m = re.search(r"Detected flash size:\s*(\S+)", esptool_output)
    return m.group(1) if m else None


def check_chip(port):
    header("Checking the board")
    rc, out = esptool_retry(port, ["flash_id"], capture=True)
    if DRY_RUN:
        return "00:00:00:00:00:00"
    if rc != 0:
        say("Could not talk to the board. Nothing was changed.")
        return None
    if not is_classic_esp32(out):
        say("This does not look like a classic ESP32. Stopping - nothing was changed.")
        return None
    size = parse_flash_size(out) or "?"
    if size != "4MB":
        say(f"Flash size reported as {size}, but this firmware needs exactly 4MB. Stopping - nothing was changed.")
        return None
    mac = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", out)
    say(f"Board OK: classic ESP32, 4 MB flash, MAC {mac.group(1) if mac else 'unknown'}")
    return mac.group(1) if mac else "unknown"


def do_backup(port, mac):
    os.makedirs(BACKUP_DIR, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    name = f"flash-backup-{stamp}-{mac.replace(':', '')}.bin"
    path = os.path.join(BACKUP_DIR, name)
    header("Backing up the complete 4 MB flash")
    say(f"Saving to backups\\{name}  (takes about 2-6 minutes)")
    rc, _ = esptool_retry(port, ["read_flash", "0x0", hex(FLASH_SIZE), path])
    if DRY_RUN:
        return path
    if rc != 0 or not os.path.exists(path) or os.path.getsize(path) != FLASH_SIZE:
        say("BACKUP FAILED. Nothing was erased.")
        return None
    digest = sha256_file(path)
    with open(path + ".sha256", "w", encoding="ascii") as f:
        f.write(f"{digest}  {name}\n")
    say(f"Backup complete: {os.path.getsize(path)} bytes, SHA-256 {digest[:16]}...")
    return path


def install(manifest, port_arg):
    variants = verify_all(manifest)
    if not variants:
        say("No valid firmware found. Please download the package again.")
        return 1
    variant = choose_variant(variants, "Choose the display type and orientation")
    if not variant:
        say("Cancelled - nothing was changed.")
        return 1
    entry = manifest["variants"][variant]
    merged = os.path.join(FW_DIR, entry["merged"]["file"])
    say(f"Selected: {variant.upper()}  ({entry['merged']['file']})")

    port = choose_port(port_arg)
    mac = check_chip(port)
    if not mac:
        return 1

    header("Important: existing data will be erased")
    say("Gridline F1 is a new firmware with its own partition layout. Installing it ERASES")
    say("the whole flash: the old firmware, its Wi-Fi password, touch calibration and settings.")
    say("They cannot be carried over. After installation you will calibrate the touch screen")
    say("and enter your Wi-Fi again (about 2 minutes).")
    say("")
    if yes_no("Make a full backup of the current flash first? (strongly recommended)", True):
        if not do_backup(port, mac):
            return 1
    else:
        say("")
        say("Without a backup you cannot go back to the firmware that is on the board now.")
        if ask("To continue WITHOUT a backup, type SKIP (anything else cancels)") != "SKIP":
            say("Cancelled - nothing was changed.")
            return 1
    if not yes_no("Erase the board and install Gridline F1 now?", True):
        say("Cancelled - nothing was erased.")
        return 1

    header("Erasing the flash")
    rc, _ = esptool_retry(port, ["erase_flash"])
    if rc != 0:
        say("Erase failed. Run the installer again; your backup (if made) is in the backups folder.")
        return 1
    header("Writing Gridline F1")
    fl = manifest["flash"]
    rc, _ = esptool_retry(port, ["write_flash", "--flash_mode", fl["mode"], "--flash_freq", fl["freq"],
                                 "--flash_size", fl["size"], "0x0", merged])
    if rc != 0:
        say("Writing failed. The board is erased; please run the installer again.")
        return 1
    say("")
    say("Firmware written and verified by esptool (hash check after writing).")
    say("The board restarts now. First you will see the touch calibration screen.")
    if yes_no("Open the serial monitor to watch the start-up log?", True):
        run_monitor(port)
    return 0


def update_app_only(manifest, port_arg):
    variants = verify_all(manifest)
    header("Update application only (keeps Wi-Fi, calibration and settings)")
    say("Only for boards that already run Gridline F1 1.x.")
    say("Note: if you already chose an orientation in Settings, the display keeps that choice.")
    variant = choose_variant(variants, "Choose the display type and orientation")
    if not variant:
        say("Cancelled - nothing was changed.")
        return 1
    entry = manifest["variants"][variant]
    port = choose_port(port_arg)
    if not check_chip(port):
        return 1
    tmp = os.path.join(LOG_DIR, "device-partitions.bin")
    rc, _ = esptool_retry(port, ["read_flash", "0x8000", "0xc00", tmp])
    ours = open(os.path.join(FW_DIR, entry["partitions"]["file"]), "rb").read()
    if not DRY_RUN:
        theirs = open(tmp, "rb").read() if rc == 0 and os.path.exists(tmp) else b""
        if theirs[:len(ours)] != ours:
            say("The board does not have the Gridline partition table, so an app-only update is not safe.")
            say("Use option 1 (full install) instead.")
            return 1
    rc, _ = esptool_retry(port, ["write_flash", "0x10000", os.path.join(FW_DIR, entry["app"]["file"])])
    say("Done." if rc == 0 else "Update failed.")
    return rc


def restore_backup(port_arg):
    header("Restore a backup")
    files = sorted(f for f in os.listdir(BACKUP_DIR) if f.endswith(".bin")) if os.path.isdir(BACKUP_DIR) else []
    if not files:
        say("No backups found in the backups folder.")
        return 1
    for i, f in enumerate(files, 1):
        say(f"  {i}) {f}")
    a = ask("Number of the backup to restore")
    if not (a.isdigit() and 1 <= int(a) <= len(files)):
        return 1
    path = os.path.join(BACKUP_DIR, files[int(a) - 1])
    if os.path.getsize(path) != FLASH_SIZE:
        say("This file is not a complete 4 MB backup.")
        return 1
    side = path + ".sha256"
    if os.path.exists(side):
        expected = open(side, encoding="ascii").read().split()[0]
        if sha256_file(path) != expected:
            say("Backup file is damaged (SHA-256 mismatch). Not restoring.")
            return 1
        say("Backup checksum OK.")
    port = choose_port(port_arg)
    if not check_chip(port):
        return 1
    if not yes_no("This erases Gridline F1 and restores the backup. Continue?", False):
        return 1
    rc, _ = esptool_retry(port, ["erase_flash"])
    if rc == 0:
        rc, _ = esptool_retry(port, ["write_flash", "--flash_mode", "keep", "--flash_freq", "keep",
                                     "--flash_size", "keep", "0x0", path])
    say("Backup restored." if rc == 0 else "Restore failed.")
    return rc


def run_monitor(port=None):
    script = os.path.join(HERE, "serial_monitor.py")
    args = [sys.executable, script] + (["--port", port] if port else [])
    if DRY_RUN:
        say("> " + " ".join(args))
        return 0
    return subprocess.call(args)


def main():
    global DRY_RUN, LOG_FILE
    ap = argparse.ArgumentParser(description="Gridline F1 installer")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--check-only", action="store_true")
    ap.add_argument("--port")
    ap.add_argument("--no-env", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()
    if not args.no_env:
        ensure_environment(sys.argv[1:])
    DRY_RUN = args.dry_run
    os.makedirs(LOG_DIR, exist_ok=True)
    LOG_FILE = open(os.path.join(LOG_DIR, "installer-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + ".log"),
                    "w", encoding="utf-8")
    try:
        manifest = load_manifest()
    except FirmwareError as e:
        say(str(e))
        return 1
    say(f"Gridline F1 installer - firmware version {manifest['version']}")
    if args.check_only:
        return 0 if len(verify_all(manifest)) == len(manifest["variants"]) else 1
    while True:
        header("What would you like to do?")
        say("  1) Install Gridline F1 (recommended - erases the board, offers a backup first)")
        say("  2) Backup only (save the current flash, change nothing)")
        say("  3) Restore a backup")
        say("  4) Serial monitor (view the board's log)")
        say("  5) Update application only (board already runs Gridline F1)")
        say("  Q) Quit")
        a = ask("Your choice", "1").lower()
        if a == "1":
            return install(manifest, args.port)
        if a == "2":
            port = choose_port(args.port)
            mac = check_chip(port)
            return 0 if (mac and do_backup(port, mac)) else 1
        if a == "3":
            return restore_backup(args.port)
        if a == "4":
            return run_monitor(args.port)
        if a == "5":
            return update_app_only(manifest, args.port)
        if a == "q":
            return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nStopped by user.")
        sys.exit(130)
