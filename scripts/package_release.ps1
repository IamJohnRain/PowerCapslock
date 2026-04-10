param(
    [string]$Version = "v0.1.2.preview"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $repoRoot "build"
$outputRoot = Join-Path $repoRoot ".release"
$stagingDir = Join-Path $outputRoot "PowerCapslock-$Version"
$zipPath = Join-Path $outputRoot "PowerCapslock-$Version.zip"
$releaseNotes = Join-Path $repoRoot "docs\releases\$Version.md"

$requiredFiles = @(
    @{ Source = Join-Path $buildDir "powercapslock.exe"; Dest = "powercapslock.exe" }
    @{ Source = Join-Path $buildDir "onnxruntime.dll"; Dest = "onnxruntime.dll" }
    @{ Source = Join-Path $buildDir "onnxruntime_providers_shared.dll"; Dest = "onnxruntime_providers_shared.dll" }
    @{ Source = Join-Path $buildDir "sherpa-onnx-c-api.dll"; Dest = "sherpa-onnx-c-api.dll" }
    @{ Source = Join-Path $buildDir "sherpa-onnx-cxx-api.dll"; Dest = "sherpa-onnx-cxx-api.dll" }
    @{ Source = Join-Path $repoRoot "scripts\install_release.bat"; Dest = "install.bat" }
    @{ Source = Join-Path $repoRoot "scripts\uninstall_release.bat"; Dest = "uninstall.bat" }
    @{ Source = Join-Path $repoRoot "config\config.json"; Dest = "config\config.json" }
    @{ Source = Join-Path $repoRoot "resources\icon.ico"; Dest = "resources\icon.ico" }
    @{ Source = Join-Path $repoRoot "resources\icon_disabled.ico"; Dest = "resources\icon_disabled.ico" }
    @{ Source = Join-Path $repoRoot "README.md"; Dest = "README.md" }
    @{ Source = $releaseNotes; Dest = "RELEASE_NOTES.md" }
)

foreach ($item in $requiredFiles) {
    if (-not (Test-Path $item.Source)) {
        throw "Required file not found: $($item.Source)"
    }
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

if (Test-Path $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
if (Test-Path $zipPath) {
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

$modelsDir = Join-Path $stagingDir "models"
New-Item -ItemType Directory -Force -Path $modelsDir | Out-Null
Set-Content -Path (Join-Path $modelsDir "README.txt") -Encoding ASCII -Value @(
    "Speech model files are not bundled in this preview package.",
    "Place the downloaded model under:",
    "models\SenseVoice-Small\sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17\"
)

Compress-Archive -Path $stagingDir -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Release package created:"
Write-Host "  $zipPath"
