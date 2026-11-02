# Prueft, dass die Versionsnummern jedes Programms an allen Stellen zusammenpassen (laeuft auch in der CI):
#   CMake-Variable  =  Versionsressource (resources\*.rc, FileVersion)  =  Rueckfall-Define im Quelltext (nur Warnung)  und
#   changelog.txt hat einen Eintrag mit mindestens dieser Version.
# Aufruf: .\tools\CheckVersions.ps1        (Exitcode 1 bei Fehlern, 0 sonst)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
function Read-Text($rel) { Get-Content (Join-Path $repo $rel) -Raw -Encoding UTF8 }
$cmake = Read-Text 'CMakeLists.txt'
$changelog = Get-Content (Join-Path $repo 'changelog.txt') -Encoding UTF8

function Get-CMakeVar($name) { if ($cmake -match ('set\(' + $name + '\s+"([0-9.]+)"\)')) { return $Matches[1] }; return $null }
function Get-RcVersion($file) { $t = Read-Text $file; if ($t -match '"FileVersion",\s*"([0-9.]+)(?:\\0)?"') { return ($Matches[1] -split '\.')[0..2] -join '.' }; return $null }
function Get-Define($file, $name) { $t = Read-Text $file; if ($t -match ('#define\s+' + $name + '\s+L"([0-9.]+)"')) { return $Matches[1] }; return $null }
function Get-ChangelogTop($exe) {
    $in = $false
    foreach ($line in $changelog) {
        if ($line -match '^\[(.+)\]\s*$') { $in = ($Matches[1] -eq $exe); continue }
        if ($in -and $line -match '^##\s*([0-9][0-9.]*)') { return $Matches[1] }
    }
    return $null
}
function Norm($v) { if (-not $v) { return $null }; $p = @($v -split '\.'); while ($p.Count -lt 3) { $p += '0' }; return [version]($p[0..2] -join '.') }

$errors = @(); $warnings = @()
$rows = @()
# Name, EXE im Changelog, Version (CMake oder anderer Hauptwert), Ressource, Rueckfall-Define (Datei, Name)
$items = @(
    @{ N = 'Suite';         Exe = 'DuncansSuite.exe';          Main = Get-CMakeVar 'SUITE_VER';        Rc = 'resources/suite.rc';        Def = @('suite/main.cpp', 'SUITE_VERSION') }
    @{ N = 'PDF Reader';    Exe = 'DuncansPdf.exe';            Main = Get-CMakeVar 'PDF_VER';          Rc = 'resources/pdf.rc';          Def = @('pdf/main.cpp', 'APP_VERSION') }
    @{ N = 'Photo';         Exe = 'DuncansPhotoCommander.exe'; Main = Get-CMakeVar 'PHOTO_VER';        Rc = 'resources/photo.rc';        Def = @('photo/main.cpp', 'APP_VERSION') }
    @{ N = 'Docs';          Exe = 'OggysDocsFiles.exe';        Main = Get-CMakeVar 'DOCS_VER';         Rc = 'resources/docs.rc';         Def = @('docs/main.cpp', 'APP_VERSION') }
    @{ N = 'Vault';         Exe = 'DuncansVault.exe';          Main = Get-CMakeVar 'VAULT_VER';        Rc = 'resources/vault.rc';        Def = @('vault/main.cpp', 'APP_VERSION') }
    @{ N = 'Office';        Exe = 'DuncansOffice.exe';         Main = Get-CMakeVar 'OFFICE_VER';       Rc = 'resources/office.rc';       Def = @('office/main.cpp', 'APP_VERSION') }
    @{ N = 'QuickConvert';  Exe = 'DuncansQuickConvert.exe';   Main = Get-CMakeVar 'QUICKCONVERT_VER'; Rc = 'resources/quickconvert.rc'; Def = @('quickconvert/main.cpp', 'APP_VERSION') }
    @{ N = 'Driver Updater'; Exe = 'DuncansDriverUpdater.exe'; Main = $(if ($cmake -match 'project\(DuncansDriverUpdater VERSION ([0-9.]+)') { $Matches[1] }); Rc = 'resources/app.rc'; Def = $null }
    @{ N = 'Privacy';       Exe = 'DuncansPrivacy.exe';        Main = $(if ($cmake -match 'DuncansPrivacyShield PRIVATE[^)]*APP_VERSION=L\\"([0-9.]+)\\"') { $Matches[1] }); Rc = 'resources/privacy.rc'; Def = @('privacy/main.cpp', 'APP_VERSION') }
)
foreach ($it in $items) {
    $main = Norm $it.Main
    $rc = if (Test-Path (Join-Path $repo $it.Rc)) { Norm (Get-RcVersion $it.Rc) } else { $null }
    $def = if ($it.Def) { Norm (Get-Define $it.Def[0] $it.Def[1]) } else { $null }
    $log = Norm (Get-ChangelogTop $it.Exe)
    if (-not $main) { $errors += "$($it.N): Hauptversion nicht gefunden"; continue }
    if ($rc -and $rc -ne $main) { $errors += "$($it.N): Ressource $rc passt nicht zu $main" }
    if ($def -and $def -ne $main) { $warnings += "$($it.N): Rueckfall-Define im Quelltext $def (Hauptversion $main)" }
    if (-not $log) { $errors += "$($it.N): kein Eintrag in changelog.txt" } elseif ($log -lt $main) { $errors += "$($it.N): changelog.txt endet bei $log, Version ist $main" }
    $rows += [pscustomobject]@{ Programm = $it.N; Version = $main; Ressource = $rc; Define = $def; Changelog = $log }
}

# Installer der Suite
if ((Read-Text 'installer/installer.iss') -match '#define MyAppVersion\s+"([0-9.]+)"') {
    $iss = Norm $Matches[1]; $suite = Norm (Get-CMakeVar 'SUITE_VER')
    if ($iss -ne $suite) { $errors += "Suite: installer.iss $iss passt nicht zu $suite" }
}
# Tidy (liegt im Repo unter tidy\)
if (Test-Path (Join-Path $repo 'tidy/src/Download-Cleaner.cpp')) {
    $t = Read-Text 'tidy/src/Download-Cleaner.cpp'
    $k = if ($t -match 'kVersion\[\]\s*=\s*L"([0-9.]+)"') { Norm $Matches[1] } else { $null }
    $rcT = Norm (Get-RcVersion 'tidy/src/version.rc')
    $logT = Norm (Get-ChangelogTop 'DuncansTidy.exe')
    if ($k -and $rcT -and $k -ne $rcT) { $errors += "Tidy: version.rc $rcT passt nicht zu kVersion $k" }
    if ($k -and (-not $logT -or $logT -lt $k)) { $errors += "Tidy: changelog.txt endet bei $logT, kVersion ist $k" }
    $rows += [pscustomobject]@{ Programm = 'Tidy'; Version = $k; Ressource = $rcT; Define = $k; Changelog = $logT }
}

$rows | Format-Table -AutoSize | Out-String | Write-Host
foreach ($w in $warnings) { Write-Warning $w }
foreach ($e in $errors) { Write-Host "FEHLER: $e" -ForegroundColor Red }
if ($errors.Count) { exit 1 }
Write-Host 'Versionen passen zusammen.'
