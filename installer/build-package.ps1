# PowerCapslock Build and Package Script
# This script builds the application and creates a distributable package

param(
    [switch]$BuildOnly,
    [switch]$ZipOnly
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $ProjectDir "build"
$DistDir = Join-Path $ProjectDir "dist"
$PackageDir = Join-Path $DistDir "PowerCapslock-1.0.0"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "PowerCapslock Package Builder" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Paths
$TesseractDir = "C:\Program Files\Tesseract-OCR"
$SherpaOnnxDir = Join-Path $ProjectDir "lib\sherpa-onnx-v1.12.36-win-x64-shared-MT-Release"
$WebView2Dir = Join-Path $ProjectDir "lib\webview2"

# Step 1: Build
if (-not $ZipOnly) {
    Write-Host "[1/4] Building PowerCapslock..." -ForegroundColor Yellow

    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir | Out-Null
    }

    Push-Location $BuildDir
    try {
        # Configure
        Write-Host "  Running CMake..." -ForegroundColor Gray
        cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

        if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }

        # Build
        Write-Host "  Running MinGW Make..." -ForegroundColor Gray
        mingw32-make -j4

        if ($LASTEXITCODE -ne 0) { throw "Build failed" }

        Write-Host "  Build completed successfully!" -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
}

# Step 2: Prepare Package Directory
Write-Host ""
Write-Host "[2/4] Preparing package directory..." -ForegroundColor Yellow

if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Path $PackageDir | Out-Null
New-Item -ItemType Directory -Path (Join-Path $PackageDir "resources") | Out-Null
New-Item -ItemType Directory -Path (Join-Path $PackageDir "tessdata") | Out-Null

# Step 3: Copy Files
Write-Host ""
Write-Host "[3/4] Copying files..." -ForegroundColor Yellow

# Main executable
Write-Host "  Copying main executable..." -ForegroundColor Gray
Copy-Item (Join-Path $BuildDir "powercapslock.exe") $PackageDir

# WebView2
Write-Host "  Copying WebView2..." -ForegroundColor Gray
Copy-Item (Join-Path $WebView2Dir "WebView2Loader.dll") $PackageDir

# Sherpa ONNX DLLs
Write-Host "  Copying Sherpa ONNX..." -ForegroundColor Gray
$SherpaOnnxLib = Join-Path $SherpaOnnxDir "lib"
Copy-Item (Join-Path $SherpaOnnxLib "onnxruntime.dll") $PackageDir
Copy-Item (Join-Path $SherpaOnnxLib "onnxruntime_providers_shared.dll") $PackageDir
Copy-Item (Join-Path $SherpaOnnxLib "sherpa-onnx-c-api.dll") $PackageDir
Copy-Item (Join-Path $SherpaOnnxLib "sherpa-onnx-cxx-api.dll") $PackageDir

# Tesseract DLLs
Write-Host "  Copying Tesseract OCR..." -ForegroundColor Gray
$TesseractDlls = Get-ChildItem $TesseractDir -Filter "*.dll" -File
foreach ($dll in $TesseractDlls) {
    Copy-Item $dll.FullName $PackageDir
}

# Resources
Write-Host "  Copying resources..." -ForegroundColor Gray
Copy-Item (Join-Path $ProjectDir "resources\config_ui.html") (Join-Path $PackageDir "resources")
Copy-Item (Join-Path $ProjectDir "resources\icon.ico") (Join-Path $PackageDir "resources")
Copy-Item (Join-Path $ProjectDir "resources\icon_disabled.ico") (Join-Path $PackageDir "resources")

# Tessdata - only necessary language packs
Write-Host "  Copying OCR language packs (English + Chinese)..." -ForegroundColor Gray
$TessdataDir = Join-Path $PackageDir "tessdata"
$TessdataSrc = Join-Path $TesseractDir "tessdata"
$RequiredLangs = @("eng.traineddata", "osd.traineddata", "chi_sim.traineddata",
                  "chi_sim_vert.traineddata", "chi_tra.traineddata", "chi_tra_vert.traineddata")
foreach ($lang in $RequiredLangs) {
    $src = Join-Path $TessdataSrc $lang
    if (Test-Path $src) {
        Copy-Item $src $TessdataDir
    }
}
# Copy configs and script directories if they exist
if (Test-Path (Join-Path $TessdataSrc "configs")) {
    Copy-Item (Join-Path $TessdataSrc "configs") $TessdataDir -Recurse
}
if (Test-Path (Join-Path $TessdataSrc "script")) {
    Copy-Item (Join-Path $TessdataSrc "script") $TessdataDir -Recurse
}
if (Test-Path (Join-Path $TessdataSrc "tessconfigs")) {
    Copy-Item (Join-Path $TessdataSrc "tessconfigs") $TessdataDir -Recurse
}

# Step 4: Create Zip
if (-not $BuildOnly) {
    Write-Host ""
    Write-Host "[4/4] Creating zip package..." -ForegroundColor Yellow

    $ZipPath = Join-Path $DistDir "PowerCapslock-1.0.0-win-x64.zip"
    Compress-Archive -Path $PackageDir -DestinationPath $ZipPath -CompressionLevel Optimal

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Package Complete!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Output:" -ForegroundColor White
    Write-Host "  Portable ZIP: $ZipPath" -ForegroundColor Gray
    Write-Host "  Package Dir:  $PackageDir" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Contents:" -ForegroundColor White
    Get-ChildItem $PackageDir | ForEach-Object { Write-Host "  $($_.Name)" -ForegroundColor Gray }
}

Write-Host ""
Write-Host "Build completed!" -ForegroundColor Green
