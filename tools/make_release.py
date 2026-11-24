#!/usr/bin/env python3
"""Build the Gridline F1 Windows release package.

    python3 tools/make_release.py            # builds both firmware variants first
    python3 tools/make_release.py --skip-build

Steps: PlatformIO build -> esptool merge_bin -> structural validation of the
merged image (bootloader 0x1000, partition table 0x8000, app 0x10000, table
contents vs. partitions_gridline_4mb.csv, app size vs. partition) -> slices
of the merged image as separate files -> manifest.json + SHA256SUMS.txt ->
package folder -> ZIP -> re-open and verify every file in the ZIP.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = re.search(r'GL_FW_VERSION "([^"]+)"', open(os.path.join(ROOT, "src", "config.h")).read()).group(1)
NAME = f"Gridline-F1-v{VERSION}"
OUT = os.path.join(ROOT, "release")
PKG = os.path.join(OUT, NAME)
VARIANTS = ["st7789", "st7789_portrait", "ili9341", "ili9341_portrait"]
FLASH = {"mode": "dio", "freq": "40m", "size": "4MB"}
APP_OFFSET, APP_SIZE = 0x10000, 0x300000


def sh(cmd, **kw):
    print("+", " ".join(cmd))
    subprocess.check_call(cmd, **kw)


def sha256(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def parse_partition_table(data):
    """Decode ESP-IDF binary partition table entries (32 bytes each)."""
    entries = []
    for i in range(0, len(data), 32):
        e = data[i:i + 32]
        if e[:2] == b"\xaa\x50":
            _, ptype, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", e)
            entries.append((label.rstrip(b"\0").decode(), ptype, subtype, offset, size))
        elif e[:2] == b"\xeb\xeb":  # MD5 entry
            continue
        else:
            break
    return entries


def expected_partitions():
    rows = []
    for line in open(os.path.join(ROOT, "partitions_gridline_4mb.csv")):
        line = line.split("#")[0].strip()
        if not line:
            continue
        cols = [c.strip() for c in line.split(",")]
        rows.append((cols[0], int(cols[3], 16), int(cols[4], 16)))
    return rows


def validate_merged(path, app_len):
    data = open(path, "rb").read()
    assert data[0x1000] == 0xE9, "no bootloader image at 0x1000"
    assert data[0x8000:0x8002] == b"\xaa\x50", "no partition table at 0x8000"
    assert data[APP_OFFSET] == 0xE9, "no app image at 0x10000"
    assert int.from_bytes(data[0x1000 + 12:0x1000 + 14], "little") == 0, "bootloader is not for classic ESP32"
    assert int.from_bytes(data[APP_OFFSET + 12:APP_OFFSET + 14], "little") == 0, "app is not for classic ESP32"
    # bootloader header: byte 2 = flash mode (2 = DIO), byte 3 high nibble = size (2 = 4MB), low = freq (0 = 40 MHz)
    assert data[0x1002] == 2, f"bootloader flash mode byte {data[0x1002]} != DIO"
    assert data[0x1003] == 0x20, f"bootloader size/freq byte 0x{data[0x1003]:02x} != 4MB/40MHz"
    table = parse_partition_table(data[0x8000:0x8C00])
    exp = expected_partitions()
    got = [(t[0], t[3], t[4]) for t in table]
    assert got == exp, f"partition table mismatch:\n got {got}\n exp {exp}"
    factory = [t for t in table if t[0] == "factory"][0]
    assert factory[3] == APP_OFFSET and factory[4] == APP_SIZE
    assert app_len <= APP_SIZE, "application larger than the factory partition"
    end = max(t[3] + t[4] for t in table)
    assert end <= 4 * 1024 * 1024, "partitions exceed 4 MB"
    return table, data


def build_firmware(skip):
    if not skip:
        for v in VARIANTS:
            sh(["pio", "run", "-e", v], cwd=ROOT)
    fw = os.path.join(PKG, "firmware")
    os.makedirs(fw, exist_ok=True)
    manifest = {
        "name": "Gridline F1", "version": VERSION, "chip": "esp32", "flash": FLASH,
        "app_partition_size": APP_SIZE,
        "built_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "toolchain": "PlatformIO espressif32@6.9.0 / arduino-esp32 2.0.17 / TFT_eSPI 2.5.43 / ArduinoJson 7.4.2",
        "notes": "merged = complete image for a full install at 0x0 (erase first). app = application only at 0x10000 "
                 "(updates of a board that already runs Gridline F1 with the same partition table).",
        "variants": {},
    }
    report = []
    for v in VARIANTS:
        b = os.path.join(ROOT, ".pio", "build", v)
        boot, part, app = (os.path.join(b, n) for n in ("bootloader.bin", "partitions.bin", "firmware.bin"))
        tag = v.upper()
        merged = os.path.join(fw, f"{tag}_FULL-INSTALL_merged_write-at-0x0.bin")
        sh([sys.executable, "-m", "esptool", "--chip", "esp32", "merge_bin", "-o", merged,
            "--flash_mode", FLASH["mode"], "--flash_freq", FLASH["freq"], "--flash_size", FLASH["size"],
            "0x1000", boot, "0x8000", part, hex(APP_OFFSET), app])
        app_len = os.path.getsize(app)
        table, data = validate_merged(merged, app_len)
        # separate files are exact slices of the merged image (what is actually written)
        parts = {
            "bootloader": (0x1000, os.path.getsize(boot), f"{tag}_part_bootloader_at-0x1000.bin"),
            "partitions": (0x8000, os.path.getsize(part), f"{tag}_part_partition-table_at-0x8000.bin"),
            "app": (APP_OFFSET, app_len, f"{tag}_APP-ONLY_update_write-at-0x10000.bin"),
        }
        entry = {"merged": {"file": os.path.basename(merged), "offset": 0, "size": os.path.getsize(merged),
                            "sha256": sha256(merged)}}
        for kind, (off, ln, fname) in parts.items():
            p = os.path.join(fw, fname)
            with open(p, "wb") as f:
                f.write(data[off:off + ln])
            entry[kind] = {"file": fname, "offset": off, "size": ln, "sha256": sha256(p)}
        assert open(os.path.join(fw, parts["app"][2]), "rb").read() == open(app, "rb").read(), "app slice differs"
        manifest["variants"][v] = entry
        used = 100.0 * app_len / APP_SIZE
        report.append(f"{tag}: app {app_len} bytes ({used:.1f}% of {APP_SIZE} byte partition), merged "
                      f"{entry['merged']['size']} bytes, partitions: " +
                      ", ".join(f"{t[0]}@0x{t[3]:x}+0x{t[4]:x}" for t in table))
    with open(os.path.join(fw, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    with open(os.path.join(fw, "README-FIRMWARE.txt"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(f"Gridline F1 {VERSION} firmware images\n\n"
                "*_FULL-INSTALL_merged_write-at-0x0.bin\n"
                "    Complete image (bootloader + partition table + application).\n"
                "    Erase the flash, then write at offset 0x0. Used by install.bat.\n\n"
                "*_APP-ONLY_update_write-at-0x10000.bin\n"
                "    Application only. Write at 0x10000 WITHOUT erasing, and only on a board that\n"
                "    already runs Gridline F1 (same partition table). Keeps Wi-Fi/calibration/settings.\n\n"
                "*_part_*.bin\n"
                "    The bootloader (0x1000) and partition table (0x8000) exactly as contained in the\n"
                "    merged image, for manual flashing.\n\n"
                "Flash settings: DIO, 40 MHz, 4MB. Chip: classic ESP32.\n"
                "Checksums: see SHA256SUMS.txt in the package root and manifest.json.\n")
    return manifest, report


def copy_tree(src, dst, ignore=None):
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns(*(ignore or [])), dirs_exist_ok=True)


def assemble(manifest):
    inst = os.path.join(ROOT, "installer")
    for f in ("install.bat", "serial-monitor.bat", "verify-files.bat"):
        shutil.copy2(os.path.join(inst, f), PKG)
    tools = os.path.join(PKG, "tools")
    os.makedirs(tools, exist_ok=True)
    for f in ("gridline_flasher.py", "serial_monitor.py", "requirements-win.lock"):
        shutil.copy2(os.path.join(inst, f), tools)
    copy_tree(os.path.join(inst, "wheels"), os.path.join(tools, "wheels"))
    # documentation (HTML for easy right-to-left reading, Markdown sources too)
    docs_src = os.path.join(ROOT, "docs")
    copy_tree(docs_src, os.path.join(PKG, "docs"), ignore=["*.tmp"])
    if os.path.exists(os.path.join(docs_src, "START-HERE.html")):
        shutil.move(os.path.join(PKG, "docs", "START-HERE.html"), os.path.join(PKG, "START-HERE.html"))
    shots = os.path.join(ROOT, "preview")
    if os.path.isdir(shots):
        copy_tree(shots, os.path.join(PKG, "preview"), ignore=["*@2x.png"] if False else [])
    # complete source code
    src_dir = os.path.join(PKG, "source")
    os.makedirs(src_dir, exist_ok=True)
    src_zip = os.path.join(src_dir, f"gridline-f1-source-v{VERSION}.zip")
    sh(["git", "archive", "--format=zip", f"--prefix=gridline-f1-v{VERSION}/", "-o", src_zip, "HEAD"], cwd=ROOT)


def write_sums():
    lines = []
    for base, _, files in os.walk(PKG):
        for fn in sorted(files):
            p = os.path.join(base, fn)
            rel = os.path.relpath(p, PKG).replace(os.sep, "/")
            if rel == "SHA256SUMS.txt":
                continue
            lines.append(f"{sha256(p)}  {rel}")
    lines.sort(key=lambda l: (not l.split("  ")[1].startswith("firmware/"), l.split("  ")[1]))
    with open(os.path.join(PKG, "SHA256SUMS.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return len(lines)


def make_zip():
    zpath = os.path.join(OUT, f"{NAME}-windows.zip")
    if os.path.exists(zpath):
        os.remove(zpath)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for base, _, files in os.walk(PKG):
            for fn in sorted(files):
                p = os.path.join(base, fn)
                z.write(p, os.path.join(NAME, os.path.relpath(p, PKG)))
    # verify: CRC of every member + SHA-256 against SHA256SUMS.txt
    with zipfile.ZipFile(zpath) as z:
        bad = z.testzip()
        assert bad is None, f"corrupt member {bad}"
        sums = z.read(f"{NAME}/SHA256SUMS.txt").decode().splitlines()
        for line in sums:
            digest, rel = line.split("  ", 1)
            assert hashlib.sha256(z.read(f"{NAME}/{rel}")).hexdigest() == digest, f"checksum mismatch {rel}"
        count = len(z.namelist())
    zsum = sha256(zpath)
    with open(zpath + ".sha256", "w", encoding="ascii", newline="\n") as f:
        f.write(f"{zsum}  {os.path.basename(zpath)}\n")
    return zpath, zsum, count, len(sums)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-build", action="store_true")
    args = ap.parse_args()
    shutil.rmtree(PKG, ignore_errors=True)
    os.makedirs(PKG)
    manifest, report = build_firmware(args.skip_build)
    assemble(manifest)
    n = write_sums()
    zpath, zsum, count, nsums = make_zip()
    print("\n".join(report))
    print(f"package: {PKG} ({n} files with checksums)")
    print(f"zip: {zpath} ({count} entries, {os.path.getsize(zpath)} bytes, all {nsums} checksums verified)")
    print(f"zip sha256: {zsum}")


if __name__ == "__main__":
    main()
