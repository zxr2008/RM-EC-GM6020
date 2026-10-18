@echo off
rem ============================================================
rem  C_project one-click build script
rem  Usage: double-click this file, or run: build.bat
rem  Output: build\C_project.elf / .hex / .bin
rem  NOTE: keep this file ASCII-only with CRLF line endings.
rem        Chinese text or LF endings will break cmd parsing.
rem ============================================================
setlocal

set ARM_GCC_BIN=D:\tools\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin
set CMAKE_BIN=D:\mingw64\bin
set NINJA_BIN=D:\mingw64\bin

if not exist "%ARM_GCC_BIN%\arm-none-eabi-gcc.exe" goto no_gcc
if not exist "CMakeLists.txt" goto no_cmake

set PATH=%ARM_GCC_BIN%;%CMAKE_BIN%;%NINJA_BIN%;%PATH%
cd /d "%~dp0"

if not exist build\CMakeCache.txt goto configure
findstr /c:"arm-none-eabi" build\CMakeCache.txt >nul 2>&1
if errorlevel 1 rmdir /s /q build

:configure
echo == CMake configure ==
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 goto failed

echo == Build ==
cmake --build build
if errorlevel 1 goto failed

echo.
echo == Symbol check ==
"%ARM_GCC_BIN%\arm-none-eabi-nm.exe" build\C_project.elf > build\symbols.txt 2>nul
findstr /c:" T MainTask" build\symbols.txt >nul
if errorlevel 1 echo    MainTask         MISSING
if not errorlevel 1 echo    MainTask         OK
findstr /c:" T ControlTaskInit" build\symbols.txt >nul
if errorlevel 1 echo    ControlTaskInit  MISSING
if not errorlevel 1 echo    ControlTaskInit  OK

echo.
echo == Done. Artifacts: ==
dir /b build\C_project.elf build\C_project.hex build\C_project.bin 2>nul
endlocal
exit /b 0

:no_gcc
echo [ERROR] arm-none-eabi-gcc not found:
echo         %ARM_GCC_BIN%
endlocal
exit /b 1

:no_cmake
echo [ERROR] CMakeLists.txt not found. Run CubeMX GENERATE CODE first.
endlocal
exit /b 1

:failed
echo.
echo [ERROR] build failed - see messages above
endlocal
exit /b 1
