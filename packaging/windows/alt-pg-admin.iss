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
  #define InstallerAppId "io.github.hdijkema.AltPgAdmin"
#endif
#ifndef InstallerName
  #define InstallerName "alt-pg-admin"
#endif

[Setup]
AppId={#InstallerAppId}
AppName={#InstallerName}
AppVersion={#AppVersion}
AppPublisher=alt-pg-admin contributors
AppPublisherURL=https://github.com/hdijkema/alt-pg-admin
DefaultDirName={localappdata}\Programs\alt-pg-admin
DefaultGroupName={#InstallerName}
DisableProgramGroupPage=yes
AllowNoIcons=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDirectory}
OutputBaseFilename=alt-pg-admin-{#AppVersion}-windows-x64-setup
SetupIconFile=..\..\assets\icons\alt-pg-admin.ico
UninstallDisplayIcon={app}\pgadmin_qt.exe
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
Name: "{autoprograms}\{#InstallerName}"; Filename: "{app}\pgadmin_qt.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\{#InstallerName}"; Filename: "{app}\pgadmin_qt.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\pgadmin_qt.exe"; Description: "{cm:LaunchProgram,{#InstallerName}}"; Flags: nowait postinstall skipifsilent
