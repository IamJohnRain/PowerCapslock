@echo off
setlocal EnableExtensions

echo ========================================
echo PowerCapslock Release Installer
echo ========================================
echo.

net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script requires administrator privileges.
    echo Please right-click and select "Run as administrator".
    pause
    exit /b 1
)

set "PACKAGE_DIR=%~dp0"
set "INSTALL_DIR=%ProgramFiles%\PowerCapslock"
set "USER_ROOT=%USERPROFILE%\.PowerCapslock"
set "CONFIG_DIR=%USER_ROOT%\config"
set "LOG_DIR=%USER_ROOT%\logs"
set "SHORTCUT_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\PowerCapslock"

if not exist "%PACKAGE_DIR%powercapslock.exe" (
    echo ERROR: powercapslock.exe not found next to this installer.
    pause
    exit /b 1
)

echo Stopping running instance...
taskkill /F /IM powercapslock.exe >nul 2>&1

echo Creating directories...
mkdir "%INSTALL_DIR%" 2>nul
mkdir "%INSTALL_DIR%\models" 2>nul
mkdir "%CONFIG_DIR%" 2>nul
mkdir "%LOG_DIR%" 2>nul
mkdir "%SHORTCUT_DIR%" 2>nul

echo Copying binaries...
copy /Y "%PACKAGE_DIR%powercapslock.exe" "%INSTALL_DIR%\" >nul
if %errorLevel% neq 0 (
    echo ERROR: Failed to copy powercapslock.exe
    pause
    exit /b 1
)

for %%F in (
    onnxruntime.dll
    onnxruntime_providers_shared.dll
    sherpa-onnx-c-api.dll
    sherpa-onnx-cxx-api.dll
) do (
    if exist "%PACKAGE_DIR%\%%F" (
        copy /Y "%PACKAGE_DIR%\%%F" "%INSTALL_DIR%\" >nul
    ) else (
        echo ERROR: Missing required runtime file %%F
        pause
        exit /b 1
    )
)

if exist "%PACKAGE_DIR%config\config.json" if not exist "%CONFIG_DIR%\config.json" (
    echo Copying default configuration...
    copy /Y "%PACKAGE_DIR%config\config.json" "%CONFIG_DIR%\config.json" >nul
)

if exist "%PACKAGE_DIR%resources\icon.ico" (
    mkdir "%INSTALL_DIR%\resources" 2>nul
    copy /Y "%PACKAGE_DIR%resources\icon.ico" "%INSTALL_DIR%\resources\" >nul
)
if exist "%PACKAGE_DIR%resources\icon_disabled.ico" (
    mkdir "%INSTALL_DIR%\resources" 2>nul
    copy /Y "%PACKAGE_DIR%resources\icon_disabled.ico" "%INSTALL_DIR%\resources\" >nul
)

echo Registering startup...
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" ^
    /v "PowerCapslock" ^
    /t REG_SZ ^
    /d "\"%INSTALL_DIR%\powercapslock.exe\"" ^
    /f >nul 2>&1

echo Creating Start Menu shortcut...
powershell -Command "$WshShell = New-Object -ComObject WScript.Shell; $Shortcut = $WshShell.CreateShortcut('%SHORTCUT_DIR%\PowerCapslock.lnk'); $Shortcut.TargetPath = '%INSTALL_DIR%\powercapslock.exe'; $Shortcut.WorkingDirectory = '%INSTALL_DIR%'; $Shortcut.Save()"

echo.
echo ========================================
echo Installation completed successfully!
echo ========================================
echo.
echo Program location: %INSTALL_DIR%
echo Config path:      %CONFIG_DIR%\config.json
echo Logs path:        %LOG_DIR%
echo Models path:      %INSTALL_DIR%\models
echo.
echo The speech model is not bundled in this preview package.
echo Extract the model into:
echo   %INSTALL_DIR%\models\SenseVoice-Small\...
echo.

pause
