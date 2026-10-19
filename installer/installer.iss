; installer.iss - Inno Setup 6 Skript fuer "Duncan's Suite"
; Schlanker Installer (nur Suite; Programme laedt die Suite per "Herunterladen"):  ISCC.exe installer\installer.iss
; Komplett-Installer (alle Programme enthalten):                                    ISCC.exe /DFull=1 installer\installer.iss

#define MyAppName      "Duncan's Driver Updater"
#define MyAppVersion   "1.1.25"
#define MyAppPublisher "Duncan"
#define MyAppExeName   "DuncansDriverUpdater.exe"
; Duncan's Suite: zentraler Launcher (Haupt-Startverknuepfung fuer Desktop und Startmenue)
#define SuiteName      "Duncan's Suite"
#define SuiteExeName   "DuncansSuite.exe"

[Setup]
; Eindeutige App-ID (nie mehr aendern, sonst erkennt Windows Updates nicht als gleiche App)
AppId={{7B3E2C41-5A9D-4F6E-9C1B-2D8A47E0F315}
AppName={#SuiteName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion={#MyAppVersion}
DefaultDirName={autopf}\DuncansDriverUpdater
DefaultGroupName={#SuiteName}
DisableProgramGroupPage=yes
OutputDir=..\release
#ifdef Full
OutputBaseFilename=DuncansSuite_Full_Setup_{#MyAppVersion}
#else
OutputBaseFilename=DuncansSuite_Setup_{#MyAppVersion}
#endif
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
UninstallDisplayName={#SuiteName}
UninstallDisplayIcon={app}\{#SuiteExeName}

[Languages]
Name: "german";  MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; Desktop-Verknuepfung fuer die Suite (standardmaessig angehakt)
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\release\{#SuiteExeName}"; DestDir: "{app}"; Flags: ignoreversion
#ifdef Full
; Komplett-Installer: alle Programme gleich mitinstallieren (auch ohne Internet nutzbar)
Source: "..\release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; Duncan's PDF Reader (PDFium wird zur Laufzeit aus pdfium.dll geladen)
Source: "..\release\DuncansPdf.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\release\pdfium.dll"; DestDir: "{app}"; Flags: ignoreversion
; Duncan's Privacy Shield (Administratorrechte, Registry-Einstellungen mit Sicherung)
Source: "..\release\DuncansPrivacy.exe"; DestDir: "{app}"; Flags: ignoreversion
; Hersteller-Tabelle fuer den Grafiktreiber-Abgleich (vom Nutzer editierbar)
Source: "..\release\vendor_releases.json"; DestDir: "{app}"; Flags: ignoreversion
#endif

[Icons]
Name: "{group}\{#SuiteName}"; Filename: "{app}\{#SuiteExeName}"
#ifdef Full
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Duncan's PDF Reader"; Filename: "{app}\DuncansPdf.exe"
Name: "{group}\Duncan's Privacy Shield"; Filename: "{app}\DuncansPrivacy.exe"
#endif
Name: "{group}\{cm:UninstallProgram,{#SuiteName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#SuiteName}"; Filename: "{app}\{#SuiteExeName}"; Tasks: desktopicon

[UninstallDelete]
; Programme, die die Suite per "Herunterladen"/Update nachgeladen hat (Tidy bleibt unberuehrt, es hat einen eigenen Installer)
Type: files; Name: "{localappdata}\DuncansDriverUpdater\DuncansDriverUpdater.exe*"
Type: files; Name: "{localappdata}\DuncansPdf\DuncansPdf.exe*"
Type: files; Name: "{localappdata}\DuncansPdf\pdfium.dll*"
Type: files; Name: "{localappdata}\DuncansPrivacy\DuncansPrivacy.exe*"
Type: dirifempty; Name: "{localappdata}\DuncansDriverUpdater"
Type: dirifempty; Name: "{localappdata}\DuncansPdf"
Type: dirifempty; Name: "{localappdata}\DuncansPrivacy"

[Run]
; "Programm nach der Installation starten"
Filename: "{app}\{#SuiteExeName}"; Description: "{cm:LaunchProgram,{#StringChange(SuiteName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
