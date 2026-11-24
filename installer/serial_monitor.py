#!/usr/bin/env python3
"""Gridline F1 serial monitor (115200 baud) with automatic CH340 port detection.

Start it with serial-monitor.bat, or:  py -3.12 tools\\serial_monitor.py [--port COM5]
Everything shown is also saved to logs\\serial-<date>.txt (useful for support).
Press Ctrl+C to quit.
"""
import argparse
import datetime
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PKG = os.path.dirname(HERE)
LOG_DIR = os.path.join(PKG, "logs")
VENV = os.path.join(HERE, ".venv")


def ensure_env():
    try:
        import serial  # noqa: F401
        return
    except ImportError:
        pass
    py = os.path.join(VENV, "Scripts" if os.name == "nt" else "bin", "python.exe" if os.name == "nt" else "python")
    if os.path.exists(py) and os.path.abspath(sys.executable) != os.path.abspath(py):
        sys.exit(subprocess.call([py, os.path.abspath(__file__)] + sys.argv[1:]))
    print("pyserial is not installed yet. Run install.bat once (it prepares the tools), then try again.")
    sys.exit(1)


def find_port():
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    ch = [p for p in ports if (p.vid == 0x1A86) or "CH340" in (p.description or "").upper()]
    if len(ch) == 1:
        return ch[0].device
    cands = ch or ports
    if not cands:
        return None
    for i, p in enumerate(cands, 1):
        print(f"  {i}) {p.device}  {p.description}")
    a = input("Number of the board's port: ").strip()
    return cands[int(a) - 1].device if a.isdigit() and 1 <= int(a) <= len(cands) else None


def main():
    ensure_env()
    import serial
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--no-reset", action="store_true", help="do not restart the board when connecting")
    args = ap.parse_args()
    port = args.port
    while not port:
        port = find_port()
        if not port:
            print("No serial port found. Connect the board (data cable) and press Enter, or Ctrl+C to quit.")
            input()
    os.makedirs(LOG_DIR, exist_ok=True)
    log_path = os.path.join(LOG_DIR, "serial-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + ".txt")
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.2
    # Keep DTR/RTS released so opening the port does not hold the ESP32 in reset/boot mode
    ser.dtr = False
    ser.rts = False
    ser.open()
    if not args.no_reset:
        # pulse EN (via RTS) to restart the board and show the boot log
        ser.rts = True
        time.sleep(0.12)
        ser.rts = False
    print(f"Serial monitor on {port} at 115200 baud. Log file: {log_path}")
    print("Press Ctrl+C to quit.\n")
    buf = b""
    with open(log_path, "w", encoding="utf-8") as log:
        try:
            while True:
                data = ser.read(512)
                if not data:
                    continue
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", errors="replace").rstrip("\r")
                    print(text)
                    log.write(text + "\n")
                    log.flush()
        except KeyboardInterrupt:
            print("\nMonitor closed.")
        except serial.SerialException as e:
            print(f"\nConnection lost: {e}")
        finally:
            ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
