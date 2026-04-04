@echo off
setlocal

echo ========================================
echo JohnHotKeyMap - Manual Build Instructions
echo ========================================
echo.
echo This project requires a C compiler and build tools.
echo.
echo Recommended setup:
echo.
echo Option 1: MinGW-w64 (Recommended for beginners)
echo   1. Download from: https://www.mingw-w64.org/
echo      or use: https://github.com/niXman/mingw-builds-binaries/releases
echo   2. Install to C:\mingw64
echo   3. Add C:\mingw64\bin to your PATH
echo   4. Download CMake from: https://cmake.org/download/
echo   5. Add CMake to PATH
echo   6. Run: scripts\build.bat
echo.
echo Option 2: Microsoft Visual Studio (MSVC)
echo   1. Install Visual Studio 2019 or later
echo   2. Install "Desktop development with C++" workload
echo   3. Open "Developer Command Prompt for VS"
echo   4. Navigate to project directory
echo   5. Run: scripts\build.bat
echo.
echo Option 3: Manual compilation (MinGW)
echo   If you have gcc available, you can compile manually:
echo.
echo   gcc -o hotkeymap.exe ^
echo       src/main.c src/hook.c src/keymap.c src/tray.c ^
echo       src/config.c src/logger.c src/keyboard_layout.c ^
echo       resources/resource.rc ^
echo       -luser32 -lkernel32 -lshell32 -lcomctl32 -lgdi32 -lole32 ^
echo       -mwindows -O2
echo.
echo ========================================
echo.
echo Checking for available compilers...
echo.

where gcc >nul 2>&1
if %errorLevel% equ 0 (
    echo [FOUND] GCC
    gcc --version
    echo.
) else (
    echo [NOT FOUND] GCC
)

where cl >nul 2>&1
if %errorLevel% equ 0 (
    echo [FOUND] MSVC
    cl 2>&1 | findstr /C:"Version"
    echo.
) else (
    echo [NOT FOUND] MSVC
)

where cmake >nul 2>&1
if %errorLevel% equ 0 (
    echo [FOUND] CMake
    cmake --version
    echo.
) else (
    echo [NOT FOUND] CMake
)

echo.
pause
