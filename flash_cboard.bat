@echo off
rem ============================================================
rem  Flash the C board (STM32F407IGHx) with DAPLink + pyOCD
rem  Usage: double-click, or drag a .hex/.elf onto this file
rem
rem  Difference from flash.bat (F103 project):
rem    - target  : stm32f407ighx  (genuine ST chip, not a clone)
rem    - clock   : 1 MHz          (clone boards need only 100 kHz)
rem  NOTE: keep this file ASCII-only with CRLF line endings.
rem ============================================================
setlocal

set PYOCD=C:\Users\zxr\.workbuddy\binaries\python\envs\default\Scripts\pyocd.exe
set TARGET=stm32f407ighx
set SPEED=1000000

if not exist "%PYOCD%" goto no_pyocd

set IMG=%~1
if "%IMG%"=="" set IMG=%~dp0build\C_project.hex

if not exist "%IMG%" goto no_img

cd /d "%~dp0"

echo == Target : %TARGET%
echo == Image  : %IMG%
echo == Flashing ...
echo.

"%PYOCD%" flash -W -t %TARGET% -f %SPEED% -M under-reset "%IMG%"

if errorlevel 1 goto failed

echo.
echo == Done. Board reset and running. ==
endlocal
exit /b 0

:no_pyocd
echo [ERROR] pyocd not found:
echo         %PYOCD%
endlocal
exit /b 1

:no_img
echo [ERROR] image not found: %IMG%
echo         run build.bat first, or drag a .hex onto this file.
endlocal
exit /b 1

:failed
echo.
echo [FAILED] check in this order:
echo   1. DAPLink GND / SWD / SCK three wires
echo   2. board powered (24V for the C board, or USB)
echo   3. CAN_H / CAN_L NOT shorted
echo   4. try a lower clock: edit SPEED to 500000 or 100000
echo   5. unplug and replug the DAPLink
endlocal
exit /b 1
