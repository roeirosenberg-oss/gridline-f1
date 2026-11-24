#!/usr/bin/env python3
"""Boot compiled Gridline F1 images in Espressif's ESP32 QEMU and check the
serial logs. QEMU emulates the CPU, flash, timers and SPI, but no display
panel, touch controller or Wi-Fi radio. These runs therefore prove that the
real binaries boot and run their logic on the Xtensa CPU - not that the
display, touch, Wi-Fi or TLS work on the physical board.

    python3 tools/qemu_boot_test.py <qemu-system-xtensa> <release-merged.bin> <selftest-merged.bin>

Scenarios
  A release image, erased flash      -> boots, formats LittleFS, starts touch calibration
  B release image, NVS pre-seeded    -> loads calibration + Wi-Fi entry, starts UI and network
                                        task; stops when the IDF Wi-Fi PHY init touches radio
                                        registers QEMU does not emulate (expected limitation)
  C self-test image (test build), 2 boots on the same flash
                                     -> on-target parsing of recorded API responses through the
                                        HTTP reader, commit + atomic LittleFS cache writes, UI loop
                                        running with data; the 2nd boot must restore the cache
"""
import base64
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

NVS_OFFSET, NVS_SIZE = 0x9000, 0x5000
CRASH = ("Guru Meditation", "abort()", "Backtrace:", "rst:0xc", "Rebooting...", "stack overflow")


def nvs_image(tmp):
    cal = struct.pack("<6f", 0.0, 0.088, -20.0, -0.066, 0.0, 262.0)
    blob = struct.pack("<I", 0x4C414347) + cal + struct.pack("<I", zlib.crc32(cal) & 0xFFFFFFFF)
    csv = os.path.join(tmp, "nvs.csv")
    with open(csv, "w") as f:
        f.write("key,type,encoding,value\ngridline,namespace,,\n")
        f.write(f"cal,data,base64,{base64.b64encode(blob).decode()}\n")
        f.write("wssid,data,string,QEMU-NoRadio\nwpass,data,string,not-a-real-password\n")
    out = os.path.join(tmp, "nvs.bin")
    subprocess.check_call([sys.executable, "-m", "esp_idf_nvs_partition_gen", "generate", csv, out, hex(NVS_SIZE)],
                          stdout=subprocess.DEVNULL)
    return open(out, "rb").read()


def flash_file(tmp, merged, seed_nvs, name):
    img = bytearray(b"\xff" * 0x400000)
    base = open(merged, "rb").read()
    img[:len(base)] = base
    if seed_nvs:
        img[NVS_OFFSET:NVS_OFFSET + NVS_SIZE] = nvs_image(tmp)
    path = os.path.join(tmp, name)
    open(path, "wb").write(img)
    return path


def boot(qemu, path, seconds):
    cmd = [qemu, "-nographic", "-machine", "esp32", "-drive", f"file={path},if=mtd,format=raw",
           "-global", "driver=timer.esp32.timg,property=wdt_disable,value=true"]
    try:
        out = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=seconds).stdout
    except subprocess.TimeoutExpired as e:
        out = e.stdout or b""
    return out.decode("utf-8", errors="replace")


def report(name, log, checks, allow=None):
    fails = 0
    print(f"== {name}")
    for label, needle in checks:
        ok = needle in log
        fails += 0 if ok else 1
        print(f"   {'PASS' if ok else 'FAIL'}  {label}")
    crash = [c for c in CRASH if c in log]
    if allow and allow in log:
        print(f"   NOTE  stopped at emulator limitation: {allow}")
    else:
        print(f"   {'PASS' if not crash else 'FAIL'}  no crash / reboot {crash if crash else ''}")
        fails += 1 if crash else 0
    if "not-a-real-password" in log:
        print("   FAIL  Wi-Fi password appeared in the log")
        fails += 1
    else:
        print("   PASS  Wi-Fi password never logged")
    for l in log.splitlines():
        if " MEM " in l or " EMU " in l:
            print("     " + l.strip())
    with open(f"qemu-{name}.log", "w") as f:
        f.write(log)
    return fails


def main():
    qemu, release, selftest = sys.argv[1:4]
    portrait = sys.argv[4] if len(sys.argv) > 4 else None
    fails = 0
    with tempfile.TemporaryDirectory() as tmp:
        if portrait:
            log = boot(qemu, flash_file(tmp, portrait, False, "p.bin"), 25)
            fails += report("P_portrait_first_boot", log, [
                ("banner", "BOOT  Gridline F1"), ("portrait rotation", "screen rotation 0 (portrait)"),
                ("LittleFS mounted", "FS    LittleFS mounted"), ("touch calibration starts", "TOUCH calibration started")])
            log = boot(qemu, flash_file(tmp, portrait, True, "p2.bin"), 25)
            fails += report("P_portrait_normal_boot", log, [
                ("calibration loaded", "TOUCH calibration loaded"), ("UI + network task started", "MEM   setup done")],
                allow="LoadStorePIFAddrError")
        log = boot(qemu, flash_file(tmp, release, False, "a.bin"), 25)
        fails += report("A_release_first_boot", log, [
            ("bootloader DIO + app start", "mode:DIO"), ("banner", "BOOT  Gridline F1"),
            ("landscape rotation", "screen rotation 1 (landscape)"),
            ("no PSRAM assumed", "PSRAM none"), ("LittleFS formatted + mounted", "FS    LittleFS mounted"),
            ("touch calibration starts", "TOUCH calibration started")])
        log = boot(qemu, flash_file(tmp, release, True, "b.bin"), 25)
        fails += report("B_release_normal_boot", log, [
            ("calibration loaded from NVS", "TOUCH calibration loaded"), ("Wi-Fi entry found", "stored credentials: yes"),
            ("UI + network task started", "MEM   setup done")], allow="LoadStorePIFAddrError")
        path = flash_file(tmp, selftest, True, "c.bin")
        log1 = boot(qemu, path, 45)
        fails += report("C1_selftest_first_boot", log1, [
            ("calendar parsed on target", "PASS calendar parsed on target"),
            ("driver standings parsed", "PASS driver standings parsed on target"),
            ("constructor standings parsed", "PASS constructor standings parsed on target"),
            ("results parsed incl. DNF", "PASS race results parsed on target (incl. DNF)"),
            ("truncated JSON rejected", "PASS truncated JSON rejected"),
            ("failed parse does not overwrite data", "PASS committed calendar untouched by failed parses"),
            ("cache written", "PASS cache written atomically"),
            ("Israel DST on target", "PASS Israel DST end 2026 on target"),
            ("self-test complete", "self-test finished: 0 failure(s)"),
            ("UI loop keeps running", "MEM   emu idle")])
        log2 = boot(qemu, path, 30)
        fails += report("C2_selftest_second_boot", log2, [
            ("cache restored after reboot", "cache restored from previous boot: yes"),
            ("cache file restored", "cache /c0.bin restored")])
    print("failures:", fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
