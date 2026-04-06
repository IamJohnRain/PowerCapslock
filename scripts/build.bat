@echo off
setlocal

echo ========================================
echo PowerCapslock Build Script
echo ========================================
echo.

:: 检查构建目录
if not exist "build" (
    echo Creating build directory...
    mkdir build
)

:: 进入构建目录
cd build

:: 检测编译器
where cmake >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: CMake not found!
    echo Please install CMake and add it to PATH.
    pause
    exit /b 1
)

:: 检测MinGW
where mingw32-make >nul 2>&1
if %errorLevel% equ 0 (
    echo Using MinGW compiler...
    set GENERATOR="MinGW Makefiles"
    set MAKE_CMD=mingw32-make
) else (
    :: 检测MSVC
    where cl >nul 2>&1
    if %errorLevel% equ 0 (
        echo Using MSVC compiler...
        set GENERATOR="NMake Makefiles"
        set MAKE_CMD=nmake
    ) else (
        echo ERROR: No suitable compiler found!
        echo Please install MinGW-w64 or MSVC.
        pause
        exit /b 1
    )
)

:: 运行CMake
echo.
echo Running CMake configuration...
cmake .. -G %GENERATOR% -DCMAKE_BUILD_TYPE=Release
if %errorLevel% neq 0 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)

:: 编译
echo.
echo Building project...
%MAKE_CMD% -j4
if %errorLevel% neq 0 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

:: 成功
echo.
echo ========================================
echo Build completed successfully!
echo Output: build\powercapslock.exe
echo ========================================
echo.

pause
