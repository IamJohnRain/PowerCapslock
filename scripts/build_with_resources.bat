@echo off
setlocal

echo ========================================
echo JohnHotKeyMap Build Script (with Resources)
echo ========================================
echo.

:: 设置MinGW路径
set "MINGW_PATH=D:\mingw64\bin"
set "GCC=%MINGW_PATH%\gcc.exe"
set "WINDRES=%MINGW_PATH%\windres.exe"

:: 检查编译器
if not exist "%GCC%" (
    echo ERROR: GCC not found at %GCC%
    pause
    exit /b 1
)

:: 创建构建目录
if not exist "build" (
    echo Creating build directory...
    mkdir build
)

:: 编译资源文件
echo.
echo Compiling resources...
"%WINDRES%" resources/resource.rc build/resource.o -O coff
if %errorLevel% neq 0 (
    echo ERROR: Failed to compile resources
    pause
    exit /b 1
)

:: 编译并链接程序
echo.
echo Compiling and linking...
"%GCC%" -o build/hotkeymap.exe ^
    src/main.c src/hook.c src/keymap.c src/tray.c ^
    src/config.c src/logger.c src/keyboard_layout.c ^
    build/resource.o ^
    -luser32 -lkernel32 -lshell32 -lcomctl32 -lgdi32 -lole32 ^
    -mwindows -O2

if %errorLevel% neq 0 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

:: 成功
echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo.
echo Output: build\hotkeymap.exe
echo.

:: 显示文件信息
for %%A in (build\hotkeymap.exe) do (
    echo File size: %%~zA bytes
)

echo.
echo You can now run the program:
echo   build\hotkeymap.exe
echo.
echo Or install it:
echo   scripts\install.bat
echo.

pause
