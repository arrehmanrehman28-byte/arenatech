; Arena Edit — Inno Setup 6 installer script.
; Branding: "Arena Edit" — Made by arenatechofficial.pages.dev
; Build the app first (tools\build.ps1), then:  iscc tools\package.iss
#define MyAppName "Arena Edit"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "arenatechofficial.pages.dev"
#define MyAppURL "https://arenatechofficial.pages.dev"
; Code-signing: tools\release.ps1 sets the SIGN_CMD preprocessor define
; (via iscc /D) when CERT_PATH + CERT_PASSWORD are present; without a
; certificate the package is built unsigned (the default).

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
; Installer wizard + Start-Menu icon: the app icon (assets/icon/app_icon.ico).
SetupIconFile=..\assets\icon\app_icon.ico
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10/11 (64-bit) only.
MinVersion=10.0
UninstallDisplayIcon={app}\ArenaEdit.exe
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
; Inno signs via the standard Sign= command template. release.ps1 imports
; the CERT_PATH .pfx into the current user's certificate store and passes
; /DSIGN_CMD, enabling this line with a signtool /a (select from store)
; command — no embedded quotes, nothing env-dependent at sign time.
#ifdef SIGN_CMD
Sign="{#SIGN_CMD}"
#endif

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
