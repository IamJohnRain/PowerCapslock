@echo off
setlocal

echo ========================================
echo JohnHotKeyMap Uninstallation Script
echo ========================================
echo.

:: 检查管理员权限
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script requires administrator privileges!
    echo Please right-click and select "Run as administrator".
    pause
    exit /b 1
)

:: 停止运行中的程序
echo Stopping running instance...
taskkill /F /IM hotkeymap.exe >nul 2>&1

:: 移除开机启动
echo Removing from startup...
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" ^
    /v "JohnHotKeyMap" ^
    /f >nul 2>&1

:: 删除程序文件
set "INSTALL_DIR=%ProgramFiles%\JohnHotKeyMap"
if exist "%INSTALL_DIR%" (
    echo Removing program files...
    rmdir /S /Q "%INSTALL_DIR%" 2>nul
)

:: 删除开始菜单快捷方式
set "SHORTCUT_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\JohnHotKeyMap"
if exist "%SHORTCUT_DIR%" (
    echo Removing start menu shortcut...
    rmdir /S /Q "%SHORTCUT_DIR%" 2>nul
)

:: 成功
echo.
echo ========================================
echo Uninstallation completed successfully!
echo ========================================
echo.
echo Configuration files are preserved at:
echo   %APPDATA%\JohnHotKeyMap
echo.
echo To completely remove, you can manually delete this directory.
echo.

pause
