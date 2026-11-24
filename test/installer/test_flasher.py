"""Unit tests for the Windows flasher logic (run on any OS: python3 test/installer/test_flasher.py)."""
import importlib.util
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location("fl", os.path.join(ROOT, "installer", "gridline_flasher.py"))
fl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fl)

ESP32_OUT = """esptool.py v4.8.1
Serial port COM7
Connecting....
Chip is ESP32-D0WD-V3 (revision v3.1)
Features: WiFi, BT, Dual Core, 240MHz, VRef calibration in efuse, Coding Scheme None
Crystal is 40MHz
MAC: 08:3a:f2:12:34:56
Uploading stub...
Running stub...
Stub running...
Manufacturer: 5e
Device: 4016
Detected flash size: 4MB
Hard resetting via RTS pin...
"""
fails = 0
def check(cond, msg):
    global fails
    print(("PASS " if cond else "FAIL ") + msg)
    fails += 0 if cond else 1

check(fl.is_classic_esp32(ESP32_OUT), "ESP32-D0WD-V3 accepted")
check(fl.is_classic_esp32("Chip is ESP32-D0WDQ6 (revision v1.0)"), "ESP32-D0WDQ6 accepted")
check(fl.is_classic_esp32("Chip is ESP32-U4WDH (revision v3.0)"), "ESP32-U4WDH accepted")
check(not fl.is_classic_esp32("Chip is ESP32-S3 (QFN56) (revision v0.2)"), "ESP32-S3 rejected")
check(not fl.is_classic_esp32("Chip is ESP32-C3 (QFN32) (revision v0.4)"), "ESP32-C3 rejected")
check(not fl.is_classic_esp32("A fatal error occurred: Failed to connect"), "no chip line rejected")
check(fl.parse_flash_size(ESP32_OUT) == "4MB", "flash size 4MB parsed")
check(fl.parse_flash_size("Detected flash size: 8MB") == "8MB", "flash size 8MB parsed")
check(fl.parse_flash_size("nothing") is None, "missing flash size")

# port detection with fake pyserial port objects
class P:
    def __init__(s, dev, vid, pid, desc): s.device, s.vid, s.pid, s.description, s.manufacturer = dev, vid, pid, desc, ""
fake = types.ModuleType("serial.tools.list_ports")
ports = [P("COM3", None, None, "Communications Port (COM3)"), P("COM9", 0x1A86, 0x7523, "USB-SERIAL CH340 (COM9)"),
         P("COM4", 0x8087, 0x0ACA, "Intel(R) Active Management")]
fake.comports = lambda: ports
sys.modules["serial"] = types.ModuleType("serial")
sys.modules["serial.tools"] = types.ModuleType("serial.tools")
sys.modules["serial.tools.list_ports"] = fake
sys.modules["serial.tools"].list_ports = fake
ch, other, allp = fl.list_candidate_ports()
check([p.device for p in ch] == ["COM9"], "CH340 found among other ports (COM number not hard-coded)")
ports[1] = P("COM12", 0x1A86, 0x7523, "USB-SERIAL CH340 (COM12)")
ch, _, _ = fl.list_candidate_ports()
check([p.device for p in ch] == ["COM12"], "CH340 found after COM number changed")
ports.append(P("COM5", 0x10C4, 0xEA60, "Silicon Labs CP210x"))
ch, other, _ = fl.list_candidate_ports()
check([p.device for p in other] == ["COM5"], "other USB-UART listed as fallback")
# variant menu: answers map to the right firmware, invalid input re-asks
answers = iter(["7", "2"])
fl.ask = lambda prompt, default=None: next(answers)
fl.say = lambda msg="": None
check(fl.choose_variant(["st7789", "st7789_portrait", "ili9341", "ili9341_portrait"], "t") == "st7789_portrait",
      "menu choice 2 = ST7789 portrait (after an invalid answer)")
answers = iter(["4"])
check(fl.choose_variant(["st7789", "st7789_portrait", "ili9341", "ili9341_portrait"], "t") == "ili9341_portrait",
      "menu choice 4 = ILI9341 portrait")
answers = iter(["1"])
check(fl.choose_variant(["st7789", "st7789_portrait", "ili9341", "ili9341_portrait"], "t") == "st7789",
      "menu choice 1 = ST7789 landscape (default)")
answers = iter(["q"])
check(fl.choose_variant(["st7789"], "t") is None, "Q cancels")
print("failures:", fails)
sys.exit(1 if fails else 0)
