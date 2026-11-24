@echo off
rem Gridline F1 - serial monitor (115200 baud)
chcp 65001 >nul
cd /d "%~dp0"
title Gridline F1 serial monitor
py -3.12 "%~dp0tools\serial_monitor.py" %*
echo.
pause
