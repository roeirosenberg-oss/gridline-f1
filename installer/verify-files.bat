@echo off
rem Gridline F1 - verify the firmware files (SHA-256, sizes, image headers) without touching the board
chcp 65001 >nul
cd /d "%~dp0"
py -3.12 "%~dp0tools\gridline_flasher.py" --check-only
echo.
pause
