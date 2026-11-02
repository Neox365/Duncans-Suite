; Inno-Setup-Skript fuer den klassischen Installations-Assistenten von Duncan's Tidy.
; Gebaut von Release.ps1 (uebergibt /DAppVersion=x.y.z). Einzeln: ISCC.exe /DAppVersion=0.8.7 DuncansTidy.iss
; Installiert pro Benutzer nach %LocalAppData%\DuncansTidy (derselbe Ordner, den das Programm selbst benutzt und aktualisiert).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{8C1E6B52-4D7A-4F0B-9A53-5D1C0A7E3B94}
AppName=Duncan's Tidy
AppVersion={#AppVersion}
AppPublisher=Duncan
AppPublisherURL=https://github.com/Neox365/Duncans-Tidy-ToolSet
DefaultDirName={localappdata}\DuncansTidy
DefaultGroupName=Duncan's Tidy
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\release
OutputBaseFilename=DuncansTidy-Setup-{#AppVersion}
SetupIconFile=..\assets\icon.ico
UninstallDisplayIcon={app}\DuncansTidy.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
AppMutex=Local\DuncansTidyWindow
CloseApplications=yes
VersionInfoVersion={#AppVersion}
VersionInfoProductName=Duncan's Tidy Setup

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "autoclean"; Description: "Downloads-Ordner alle 30 Tage automatisch aufräumen (alles landet erst im Papierkorb)"; GroupDescription: "Optionen:"; Languages: german
Name: "autoclean"; Description: "Tidy the Downloads folder automatically every 30 days (everything goes to the Recycle Bin first)"; GroupDescription: "Options:"; Languages: english
Name: "desktopicon"; Description: "Verknüpfung auf dem Desktop erstellen"; GroupDescription: "Optionen:"; Flags: unchecked; Languages: german
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Options:"; Flags: unchecked; Languages: english

[Files]
Source: "..\release\DuncansTidy.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Duncan's Tidy"; Filename: "{app}\DuncansTidy.exe"
Name: "{autodesktop}\Duncan's Tidy"; Filename: "{app}\DuncansTidy.exe"; Tasks: desktopicon

[Run]
; Geplante Aufgabe fuer den stillen 30-Tage-Check beim Anmelden (wie die Selbst-Installation des Programms)
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command ""$d='{app}\DuncansTidy.exe'; $a=New-ScheduledTaskAction -Execute $d -Argument '--auto' -WorkingDirectory (Split-Path $d); $t=New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME; $s=New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries; Register-ScheduledTask -TaskName 'DuncansTidy' -Action $a -Trigger $t -Settings $s -Force | Out-Null"""; Flags: runhidden waituntilterminated
Filename: "{app}\DuncansTidy.exe"; Description: "{cm:LaunchProgram,Duncan's Tidy}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command ""Unregister-ScheduledTask -TaskName 'DuncansTidy' -Confirm:$false -ErrorAction SilentlyContinue; Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'DuncansTidy' -ErrorAction SilentlyContinue"""; Flags: runhidden waituntilterminated; RunOnceId: "RemoveTidyTask"

[UninstallDelete]
; Reste von Selbst-Updates
Type: files; Name: "{app}\DuncansTidy.exe.old"
Type: files; Name: "{app}\DuncansTidy.exe.new"

[Code]
// Auto-Clean-Auswahl: das Programm liest die Marker-Datei auto_off.flag (vorhanden = aus)
procedure CurStepChanged(CurStep: TSetupStep);
var
  Dir, Flag: String;
begin
  if CurStep = ssPostInstall then
  begin
    Dir := ExpandConstant('{localappdata}\DownloadsCleaner');
    Flag := Dir + '\auto_off.flag';
    ForceDirectories(Dir);
    if WizardIsTaskSelected('autoclean') then
      DeleteFile(Flag)
    else
      SaveStringToFile(Flag, '1', False);
  end;
end;
