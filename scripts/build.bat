@echo off
setlocal

echo ========================================
echo PowerCapslock Build Script
echo ========================================
echo.

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul

echo Stopping running PowerCapslock instances...
taskkill /IM powercapslock.exe /F >nul 2>nul

where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: CMake not found!
    echo Please install CMake and add it to PATH.
    popd >nul
    exit /b 1
)

if not exist "build" (
    echo Creating build directory...
    mkdir build
)

echo.
echo Running CMake configuration...
if exist "build\CMakeCache.txt" (
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
) else (
    where mingw32-make >nul 2>&1
    if %errorlevel% equ 0 (
        cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    ) else (
        where ninja >nul 2>&1
        if %errorlevel% equ 0 (
            cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
        ) else (
            cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
        )
    )
)
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed!
    popd >nul
    exit /b 1
)

echo.
echo Building project...
cmake --build build --config Release --parallel
if %errorlevel% neq 0 (
    echo ERROR: Build failed!
    popd >nul
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully!
echo Output: build\powercapslock.exe
echo ========================================
echo.

popd >nul
