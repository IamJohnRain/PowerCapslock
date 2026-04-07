@echo off
setlocal

echo ========================================
echo PowerCapslock Build Script
echo ========================================
echo.

:: Check build directory
if not exist "build" (
    echo Creating build directory...
    mkdir build
)

:: Enter build directory
cd build

:: Detect compiler
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: CMake not found!
    echo Please install CMake and add it to PATH.
    cd ..
    exit /b 1
)

:: Detect MinGW
where mingw32-make >nul 2>&1
if %errorlevel% equ 0 (
    echo Using MinGW compiler...
    set GENERATOR=MinGW Makefiles
    set MAKE_CMD=mingw32-make
    goto :build
)

:: Detect MSVC
where cl >nul 2>&1
if %errorlevel% equ 0 (
    echo Using MSVC compiler...
    set GENERATOR=NMake Makefiles
    set MAKE_CMD=nmake
    goto :build
)

echo ERROR: No suitable compiler found!
echo Please install MinGW-w64 or MSVC.
cd ..
exit /b 1

:build
:: Run CMake
echo.
echo Running CMake configuration...
cmake .. -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed!
    cd ..
    exit /b 1
)

:: Build
echo.
echo Building project...
%MAKE_CMD% -j4
if %errorlevel% neq 0 (
    echo ERROR: Build failed!
    cd ..
    exit /b 1
)

:: Success
echo.
echo ========================================
echo Build completed successfully!
echo Output: build\powercapslock.exe
echo ========================================
echo.

cd ..
