@echo off
setlocal

echo ========================================
echo JohnHotKeyMap - 托盘图标诊断工具
echo ========================================
echo.
echo 此脚本将帮助诊断托盘图标问题
echo.

echo 1. 检查可执行文件
if exist "build\hotkeymap.exe" (
    echo    [OK] 可执行文件存在
    for %%A in (build\hotkeymap.exe) do echo    大小: %%~zA 字节
) else (
    echo    [ERROR] 可执行文件不存在
    pause
    exit /b 1
)

echo.
echo 2. 检查资源文件
if exist "build\resource.o" (
    echo    [OK] 资源对象文件存在
) else (
    echo    [WARN] 资源对象文件不存在
)

echo.
echo 3. 检查图标文件
if exist "resources\icon.ico" (
    echo    [OK] 正常图标存在
) else (
    echo    [ERROR] 正常图标不存在
)

if exist "resources\icon_disabled.ico" (
    echo    [OK] 禁用图标存在
) else (
    echo    [ERROR] 禁用图标不存在
)

echo.
echo 4. 检查配置目录
set "CONFIG_DIR=%APPDATA%\JohnHotKeyMap"
if not exist "%CONFIG_DIR%" (
    echo    创建配置目录...
    mkdir "%CONFIG_DIR%"
    mkdir "%CONFIG_DIR%\logs"
)

echo.
echo 5. 运行程序并捕获日志
echo    日志文件: %CONFIG_DIR%\logs\hotkeymap.log
echo.
echo    请查看日志中的以下信息：
echo    - "Normal icon loaded successfully" 或 "Failed to load normal icon"
echo    - "Disabled icon loaded successfully" 或 "Failed to load disabled icon"
echo    - "Tray icon added successfully" 或 "Failed to add tray icon"
echo.

echo ========================================
echo 现在启动程序...
echo ========================================
echo.

start "" "build\hotkeymap.exe"

echo 程序已启动！
echo.
echo 请检查：
echo 1. 系统托盘（右下角）是否出现图标
echo 2. 图标是绿色还是默认应用图标
echo 3. 左键点击是否能切换状态
echo 4. 右键点击是否显示菜单
echo.
echo 如果图标仍然不显示，请查看日志文件：
echo %CONFIG_DIR%\logs\hotkeymap.log
echo.

pause
