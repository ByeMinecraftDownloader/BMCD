; Inno Setup installer for the staged BMCD Windows build.
; Compile through packaging/windows/build-installer.ps1, which supplies the
; version, stage directory and output directory as /D macros.
#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#ifndef StageDir
  #define StageDir "..\..\dist\BMCD"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{8E3F3D2B-6D4A-4E8C-9C1B-8B4E6F2A7C10}
AppName=BMCD
AppVerName=BMCD {#MyAppVersion}
AppVersion={#MyAppVersion}
AppPublisher=BMCD
AppPublisherURL=https://github.com/ByeMinecraftDownloader/BMCD
AppSupportURL=https://github.com/ByeMinecraftDownloader/BMCD/issues
AppUpdatesURL=https://github.com/ByeMinecraftDownloader/BMCD/releases
DefaultDirName={autopf}\BMCD
DefaultGroupName=BMCD
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir={#OutputDir}
OutputBaseFilename=BMCD-Setup-{#MyAppVersion}-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bmcd.exe
LicenseFile=..\..\LICENSE
VersionInfoVersion={#MyAppVersion}.0
VersionInfoProductName=BMCD
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\BMCD"; Filename: "{app}\bmcd.exe"
Name: "{autodesktop}\BMCD"; Filename: "{app}\bmcd.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\bmcd.exe"; Description: "{cm:LaunchProgram,BMCD}"; Flags: nowait postinstall skipifsilent
