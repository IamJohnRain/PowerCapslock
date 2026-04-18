param(
    [string]$Version = "v0.2.0",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $repoRoot "build"
$releaseDir = Join-Path $repoRoot ".release\$Version"
$versionName = $Version.TrimStart("v")
$stagingDir = Join-Path $releaseDir "PowerCapslock-$Version"
$zipPath = Join-Path $releaseDir "PowerCapslock-$Version-windows-x64.zip"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.bat")
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE"
    }
}

$requiredFiles = @(
    @{ Source = Join-Path $buildDir "powercapslock.exe"; Dest = "powercapslock.exe" }
    @{ Source = Join-Path $buildDir "WebView2Loader.dll"; Dest = "WebView2Loader.dll" }
    @{ Source = Join-Path $buildDir "onnxruntime.dll"; Dest = "onnxruntime.dll" }
    @{ Source = Join-Path $buildDir "onnxruntime_providers_shared.dll"; Dest = "onnxruntime_providers_shared.dll" }
    @{ Source = Join-Path $buildDir "sherpa-onnx-c-api.dll"; Dest = "sherpa-onnx-c-api.dll" }
    @{ Source = Join-Path $buildDir "sherpa-onnx-cxx-api.dll"; Dest = "sherpa-onnx-cxx-api.dll" }
)

$resourcesDir = Join-Path $repoRoot "resources"

foreach ($item in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $item.Source)) {
        throw "Required file not found: $($item.Source)"
    }
}

if (-not (Test-Path -LiteralPath $resourcesDir)) {
    throw "Required resources directory not found: $resourcesDir"
}

New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null

if (Test-Path -LiteralPath $stagingDir) {
    $resolvedStage = (Resolve-Path $stagingDir).Path
    if (-not $resolvedStage.StartsWith($releaseDir, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected path: $resolvedStage"
    }
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}

if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

New-Item -ItemType Directory -Force -Path $stagingDir | Out-Null

foreach ($item in $requiredFiles) {
    $destination = Join-Path $stagingDir $item.Dest
    $destinationDir = Split-Path -Parent $destination
    if ($destinationDir) {
        New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
    }
    Copy-Item -LiteralPath $item.Source -Destination $destination -Force
}

Copy-Item -LiteralPath $resourcesDir -Destination (Join-Path $stagingDir "resources") -Recurse -Force

Set-Content -Path (Join-Path $stagingDir "README.txt") -Encoding UTF8 -Value @(
    "PowerCapslock $versionName",
    "",
    "Usage:",
    "1. Extract this zip to any directory.",
    "2. Run powercapslock.exe.",
    "3. For voice input, download PowerCapslock-$Version-model-SenseVoice-Small.zip separately.",
    "4. Open the configuration page from the tray menu, choose the model directory, then save.",
    "5. If the model is valid, PowerCapslock loads it immediately without restart.",
    "",
    "Note: model files are not included in this main package."
)

Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Release package created:"
Write-Host "  $zipPath"
