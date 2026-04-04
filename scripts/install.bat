@echo off
setlocal

echo ========================================
echo JohnHotKeyMap Installation Script
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

:: 设置安装目录
set "INSTALL_DIR=%ProgramFiles%\JohnHotKeyMap"
set "CONFIG_DIR=%APPDATA%\JohnHotKeyMap"

:: 检查可执行文件
if not exist "build\hotkeymap.exe" (
    echo ERROR: hotkeymap.exe not found!
    echo Please run build.bat first.
    pause
    exit /b 1
)

:: 创建目录
echo Creating directories...
mkdir "%INSTALL_DIR%" 2>nul
mkdir "%CONFIG_DIR%" 2>nul
mkdir "%CONFIG_DIR%\logs" 2>nul

:: 复制程序文件
echo Copying program files...
copy /Y "build\hotkeymap.exe" "%INSTALL_DIR%\" >nul
if %errorLevel% neq 0 (
    echo ERROR: Failed to copy hotkeymap.exe
    pause
    exit /b 1
)

:: 复制图标文件
if exist "resources\icon.ico" (
    copy /Y "resources\icon.ico" "%INSTALL_DIR%\" >nul
)
if exist "resources\icon_disabled.ico" (
    copy /Y "resources\icon_disabled.ico" "%INSTALL_DIR%\" >nul
)

:: 复制默认配置（如果不存在）
if not exist "%CONFIG_DIR%\keymap.json" (
    echo Copying default configuration...
    copy /Y "config\keymap.json" "%CONFIG_DIR%\" >nul
)

:: 添加到开机启动
echo Adding to startup...
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" ^
    /v "JohnHotKeyMap" ^
    /t REG_SZ ^
    /d "\"%INSTALL_DIR%\hotkeymap.exe\"" ^
    /f >nul 2>&1

:: 创建开始菜单快捷方式
echo Creating start menu shortcut...
set "SHORTCUT_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\JohnHotKeyMap"
mkdir "%SHORTCUT_DIR%" 2>nul

powershell -Command "$WshShell = New-Object -ComObject WScript.Shell; $Shortcut = $WshShell.CreateShortcut('%SHORTCUT_DIR%\JohnHotKeyMap.lnk'); $Shortcut.TargetPath = '%INSTALL_DIR%\hotkeymap.exe'; $Shortcut.WorkingDirectory = '%CONFIG_DIR%'; $Shortcut.Save()"

:: 成功
echo.
echo ========================================
echo Installation completed successfully!
echo ========================================
echo.
echo Program location: %INSTALL_DIR%
echo Configuration:    %CONFIG_DIR%
echo Logs:             %CONFIG_DIR%\logs
echo.
echo The program will start automatically with Windows.
echo.
echo You can now:
echo   1. Launch the program from the Start Menu
echo   2. Or run: "%INSTALL_DIR%\hotkeymap.exe"
echo.

pause
