@echo off
rem Gridline F1 - Windows installer launcher
chcp 65001 >nul
cd /d "%~dp0"
title Gridline F1 installer
where py >nul 2>&1
if errorlevel 1 (
  echo.
  echo The Python launcher "py" was not found.
  echo Install Python 3.12 from https://www.python.org/downloads/windows/
  echo and tick "Add python.exe to PATH" during setup. Then run this file again.
  echo.
  pause
  exit /b 1
)
py -3.12 -c "import sys" >nul 2>&1
if errorlevel 1 (
  echo.
  echo Python 3.12 was not found by "py -3.12".
  echo Install Python 3.12 from https://www.python.org/downloads/windows/ and run this file again.
  echo.
  pause
  exit /b 1
)
py -3.12 "%~dp0tools\gridline_flasher.py" %*
echo.
pause
