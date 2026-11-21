; Inno Setup script - built by .github/workflows/build.yml
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist"
#endif

[Setup]
AppId={{6C1C2E7A-4E2B-4C8E-9B1F-8D3A2C1E5F10}
AppName=ClipStudio
AppVersion={#AppVersion}
AppPublisher=ClipStudio
DefaultDirName={autopf}\ClipStudio
DefaultGroupName=ClipStudio
SetupIconFile=..\assets\ClipStudio.ico
UninstallDisplayIcon={app}\ClipStudio.exe
OutputDir=..\out
OutputBaseFilename=ClipStudio-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0.19041
PrivilegesRequired=admin
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "autostart"; Description: "Start ClipStudio when Windows starts (hidden in the tray)"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "vc_redist.x64.exe"
Source: "{#SourceDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall skipifsourcedoesntexist

[Icons]
Name: "{group}\ClipStudio"; Filename: "{app}\ClipStudio.exe"
Name: "{group}\Uninstall ClipStudio"; Filename: "{uninstallexe}"
Name: "{autodesktop}\ClipStudio"; Filename: "{app}\ClipStudio.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "ClipStudio"; ValueData: """{app}\ClipStudio.exe"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Visual C++ runtime..."; Flags: waituntilterminated skipifdoesntexist
Filename: "{app}\ClipStudio.exe"; Description: "Launch ClipStudio"; Flags: nowait postinstall skipifsilent
; Auto-updates install silently: start the new version afterwards.
Filename: "{app}\ClipStudio.exe"; Flags: nowait; Check: WizardSilent
