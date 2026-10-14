; Arena Edit — Inno Setup 6 installer script.
; Branding: "Arena Edit" — Made by arenatechofficial.pages.dev
; Build the app first (tools\build.ps1), then:  iscc tools\package.iss
#define MyAppName "Arena Edit"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "arenatechofficial.pages.dev"
#define MyAppURL "https://arenatechofficial.pages.dev"
; Code-signing: set CERT_PATH / CERT_PASSWORD env vars before running iscc
; for an authenticated build; without them the package is unsigned.

[Setup]
; Stable AppId (GUID) — keep constant across versions so upgrades replace
; cleanly. Do not change in a released build.
AppId={{66AB53E9-3889-4EC5-B5C6-40BFCFC98E57}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\ArenaEdit
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=dist
OutputBaseFilename=ArenaEdit-{#MyAppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10/11 (64-bit) only.
MinVersion=10.0
UninstallDisplayIcon={app}\ArenaEdit.exe
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
SignWithSigntoolEnv=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; Portable payload: ArenaEdit.exe + Qt/FFmpeg DLLs + assets. The build script
; stages this folder; keep it a single directory so the exe stays relocatable.
Source: "dist\portable\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\ArenaEdit.exe"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\ArenaEdit.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\ArenaEdit.exe"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\ArenaEdit"

[Code]
// Surface the brand credit on the finished page.
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    WizardForm.WizardSmallCaptionLabel.Caption := '{#MyAppName} — Made by arenatechofficial.pages.dev';
end;
