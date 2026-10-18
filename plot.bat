@echo off
rem ============================================================
rem  Read variables from the running MCU and plot them.
rem  Double-click: plots the velocity loop for 5 seconds.
rem  With arguments: passes them to plot_vars.py
rem    plot.bat --vars g_pos_ref g_pos_fdb
rem    plot.bat --vars g_volt --dur 8
rem    plot.bat --demo
rem  NOTE: keep this file ASCII-only with CRLF line endings.
rem ============================================================
setlocal

set PY=C:\Users\zxr\.workbuddy\binaries\python\envs\default\Scripts\python.exe

if not exist "%PY%" (
  echo [ERROR] python not found: %PY%
  exit /b 1
)

cd /d "%~dp0"

if "%~1"=="" (
  "%PY%" -u tools\plot_vars.py --vars g_vel_ref g_vel_fdb --dur 6
) else (
  "%PY%" -u tools\plot_vars.py %*
)

endlocal
