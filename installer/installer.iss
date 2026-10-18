; installer.iss - Inno Setup 6 Skript fuer "Duncan's Driver Updater"
; Voraussetzung: Release-Build liegt unter ..\DuncansDriverUpdater.exe und ..\DuncansSuite.exe
; Kompilieren: ISCC.exe installer\installer.iss  (Ausgabe: installer\Output\)

#define MyAppName      "Duncan's Driver Updater"
#define MyAppVersion   "1.0.7"
#define MyAppPublisher "Duncan"
#define MyAppExeName   "DuncansDriverUpdater.exe"
; Duncan's Suite: zentraler Launcher (Haupt-Startverknuepfung fuer Desktop und Startmenue)
#define SuiteName      "Duncan's Suite"
#define SuiteExeName   "DuncansSuite.exe"

[Setup]
; Eindeutige App-ID (nie mehr aendern, sonst erkennt Windows Updates nicht als gleiche App)
AppId={{7B3E2C41-5A9D-4F6E-9C1B-2D8A47E0F315}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion={#MyAppVersion}
DefaultDirName={autopf}\DuncansDriverUpdater
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=DuncansSuite_Setup_{#MyAppVersion}
; Treiber-Operationen erfordern Administratorrechte
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Staerkste Kompression
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
SetupIconFile=..\resources\app.ico
; Eintrag unter "Apps & Features" inkl. Deinstaller (unins000.exe)
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "german";  MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; Desktop-Verknuepfung fuer die Suite (standardmaessig angehakt)
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\{#SuiteExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; Duncan's PDF Reader (PDFium wird zur Laufzeit aus pdfium.dll geladen)
Source: "..\DuncansPdf.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\pdfium.dll"; DestDir: "{app}"; Flags: ignoreversion
; Hersteller-Tabelle fuer den Grafiktreiber-Abgleich (vom Nutzer editierbar)
Source: "..\vendor_releases.json"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#SuiteName}"; Filename: "{app}\{#SuiteExeName}"
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Duncan's PDF Reader"; Filename: "{app}\DuncansPdf.exe"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#SuiteName}"; Filename: "{app}\{#SuiteExeName}"; Tasks: desktopicon

[Run]
; "Programm nach der Installation starten"
Filename: "{app}\{#SuiteExeName}"; Description: "{cm:LaunchProgram,{#StringChange(SuiteName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
