; PowerCapslock Inno Setup Script
; Download Inno Setup from https://jrsoftware.org/isinfo.php

#define MyAppName "PowerCapslock"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "PowerCapslock"
#define MyAppURL "https://github.com/IamJohnRain/PowerCapslock"
#define MyAppExeName "powercapslock.exe"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=..\dist
OutputBaseFilename=PowerCapslock-{#MyAppVersion}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Main executable
Source: "..\build\powercapslock.exe"; DestDir: "{app}"; Flags: ignoreversion

; WebView2
Source: "..\lib\webview2\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion

; Sherpa ONNX DLLs
Source: "..\lib\sherpa-onnx-v1.12.36-win-x64-shared-MT-Release\lib\onnxruntime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\lib\sherpa-onnx-v1.12.36-win-x64-shared-MT-Release\lib\onnxruntime_providers_shared.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\lib\sherpa-onnx-v1.12.36-win-x64-shared-MT-Release\lib\sherpa-onnx-c-api.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\lib\sherpa-onnx-v1.12.36-win-x64-shared-MT-Release\lib\sherpa-onnx-cxx-api.dll"; DestDir: "{app}"; Flags: ignoreversion

; Tesseract main DLL
Source: "C:\Program Files\Tesseract-OCR\libtesseract-5.dll"; DestDir: "{app}"; Flags: ignoreversion

; Tesseract dependency DLLs
Source: "C:\Program Files\Tesseract-OCR\*.dll"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Resources
Source: "..\resources\config_ui.html"; DestDir: "{app}\resources"; Flags: ignoreversion
Source: "..\resources\icon.ico"; DestDir: "{app}\resources"; Flags: ignoreversion
Source: "..\resources\icon_disabled.ico"; DestDir: "{app}\resources"; Flags: ignoreversion

; Tessdata (OCR language packs)
Source: "C:\Program Files\Tesseract-OCR\tessdata\*"; DestDir: "{app}\tessdata"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
function IsTesseractInstalled: Boolean;
begin
  Result := DirExists(ExpandConstant('{pf}\Tesseract-OCR'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Copy tessdata if not installed system-wide
    if not IsTesseractInstalled then
    begin
      // tessdata was copied with [Files] section
    end;
  end;
end;
