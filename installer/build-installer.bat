@echo off
setlocal enabledelayedexpansion

echo ================================================
echo PowerCapslock MSI Installer Builder
echo ================================================

REM Check for WiX
where candle.exe >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: WiX Toolset is not installed.
    echo Please install WiX Toolset v4 from: https://wixtoolset.org/
    echo Or install via winget: winget install WiX.WiXToolset
    exit /b 1
)

REM Set directories
set "PROJECT_DIR=%~dp0.."
set "BUILD_DIR=%PROJECT_DIR%\build"
set "INSTALLER_DIR=%PROJECT_DIR%\installer"
set "OUTPUT_DIR=%PROJECT_DIR%\dist"

REM Clean previous build
if exist "%OUTPUT_DIR%" rmdir /s /q "%OUTPUT_DIR%"
mkdir "%OUTPUT_DIR%"

REM Build the project
echo.
echo [1/4] Building PowerCapslock...
cd /d "%PROJECT_DIR%\build"
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed
    exit /b 1
)
mingw32-make -j4
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed
    exit /b 1
)

REM Copy build output to staging
echo.
echo [2/4] Copying build output...
mkdir "%OUTPUT_DIR%\PowerCapslock"
xcopy /e /y "%BUILD_DIR%\*.exe" "%OUTPUT_DIR%\PowerCapslock\" >nul
xcopy /e /y "%BUILD_DIR%\*.dll" "%OUTPUT_DIR%\PowerCapslock\" >nul
xcopy /e /y "%PROJECT_DIR%\resources" "%OUTPUT_DIR%\PowerCapslock\resources\" >nul

REM Build MSI with WiX
echo.
echo [3/4] Building MSI installer...
cd /d "%INSTALLER_DIR%"

REM Set WiX variables
set "SOURCE_DIR=%OUTPUT_DIR%\PowerCapslock"
set "SHERPA_ONNX_DIR=%PROJECT_DIR%\lib\sherpa-onnx-v1.12.36-win-x64-shared-MT-Release"
set "TESSERACT_DIR=C:\Program Files\Tesseract-OCR"

REM Build the MSI
candle.exe -d "%OUTPUT_DIR%" ^
    -d SourceDir="%SOURCE_DIR%" ^
    -d SherpaOnnxDir="%SHERPA_ONNX_DIR%" ^
    -d TesseractDir="%TESSERACT_DIR%" ^
    PowerCapslock.wxs

if %ERRORLEVEL% neq 0 (
    echo ERROR: MSI build failed
    exit /b 1
)

REM Link the MSI
light.exe -o "%OUTPUT_DIR%\PowerCapslock-1.0.0.msi" "%OUTPUT_DIR%\*.wixobj"
if %ERRORLEVEL% neq 0 (
    echo ERROR: MSI linking failed
    exit /b 1
)

REM Cleanup intermediate files
del /q "%OUTPUT_DIR%\*.wixobj" >nul 2>&1
del /q "%OUTPUT_DIR%\*.wixpdb" >nul 2>&1

echo.
echo [4/4] Cleaning up...
echo.

echo ================================================
echo Build Complete!
echo.
echo MSI Installer: %OUTPUT_DIR%\PowerCapslock-1.0.0.msi
echo.
echo Note: You may need to run as Administrator to install.
echo ================================================

endlocal
