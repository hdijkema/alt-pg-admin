; Package the already-validated portable staging directory without changing user data.
#ifndef AppVersion
  #error AppVersion must be supplied by the packaging script
#endif
#ifndef SourceDirectory
  #error SourceDirectory must be supplied by the packaging script
#endif
#ifndef OutputDirectory
  #error OutputDirectory must be supplied by the packaging script
#endif
#ifndef InstallerAppId
  ; Stable upgrade identity from the original product name.
  #define UpgradeLegacyProduct 1
  #define InstallerAppId "io.github.hdijkema.AltPgAdmin"
#endif
#ifndef InstallerName
  #define InstallerName "pgadmin-jr"
#endif

[Setup]
AppId={#InstallerAppId}
AppName={#InstallerName}
AppVersion={#AppVersion}
AppPublisher=pgadmin-jr contributors
AppPublisherURL=https://github.com/hdijkema/pgadmin-jr
DefaultDirName={localappdata}\Programs\pgadmin-jr
DefaultGroupName={#InstallerName}
DisableProgramGroupPage=yes
AllowNoIcons=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDirectory}
OutputBaseFilename=pgadmin-jr-{#AppVersion}-windows-x64-setup
SetupIconFile=..\..\assets\icons\pgadmin-jr.ico
UninstallDisplayIcon={app}\pgadmin-jr.exe
LicenseFile=..\..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
UninstallDisplayName={#InstallerName} {#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDirectory}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#InstallerName}"; Filename: "{app}\pgadmin-jr.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\{#InstallerName}"; Filename: "{app}\pgadmin-jr.exe"; WorkingDir: "{app}"; Tasks: desktopicon

#ifdef UpgradeLegacyProduct
[InstallDelete]
; Remove superseded program files/shortcuts only; user SQL and settings are retained.
Type: files; Name: "{app}\pgadmin_qt.exe"
Type: files; Name: "{autoprograms}\alt-pg-admin.lnk"
Type: files; Name: "{autodesktop}\alt-pg-admin.lnk"
#endif

[Run]
Filename: "{app}\pgadmin-jr.exe"; Description: "{cm:LaunchProgram,{#InstallerName}}"; Flags: nowait postinstall skipifsilent
