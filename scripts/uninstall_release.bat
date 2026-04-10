@echo off
setlocal EnableExtensions

echo ========================================
echo PowerCapslock Release Uninstaller
echo ========================================
echo.

net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script requires administrator privileges.
    echo Please right-click and select "Run as administrator".
    pause
    exit /b 1
)

set "INSTALL_DIR=%ProgramFiles%\PowerCapslock"
set "SHORTCUT_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\PowerCapslock"
set "USER_ROOT=%USERPROFILE%\.PowerCapslock"

echo Stopping running instance...
taskkill /F /IM powercapslock.exe >nul 2>&1

echo Removing startup registration...
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" ^
    /v "PowerCapslock" ^
    /f >nul 2>&1

if exist "%INSTALL_DIR%" (
    echo Removing installed files...
    rmdir /S /Q "%INSTALL_DIR%" 2>nul
)

if exist "%SHORTCUT_DIR%" (
    echo Removing Start Menu shortcut...
    rmdir /S /Q "%SHORTCUT_DIR%" 2>nul
)

echo.
echo ========================================
echo Uninstallation completed successfully!
echo ========================================
echo.
echo User data is preserved at:
echo   %USER_ROOT%
echo.

pause
