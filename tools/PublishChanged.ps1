# Veroeffentlicht alle Programme, deren Version lokal hoeher ist als die veroeffentlichte - in einem Durchgang.
#   .\tools\PublishChanged.ps1 -DryRun          nur anzeigen, was sich aendern wuerde (laedt nichts hoch)
#   .\tools\PublishChanged.ps1 -Build           vorher alle Programme bauen (Release), dann vergleichen
#   .\tools\PublishChanged.ps1 -Build -Yes     ohne Rueckfrage veroeffentlichen
#   .\tools\PublishChanged.ps1 -Only Vault,Docs  nur diese Programme beruecksichtigen
# Ablauf: (1) optional bauen, (2) Versionen der EXE-Dateien in release\ mit den veroeffentlichten vergleichen (modules.txt im Release "modules",
# version.txt im PDF-Release), (3) pruefen, dass changelog.txt zu jeder neuen Version einen Eintrag hat, (4) signieren und per PublishModule.ps1
# hochladen, (5) die veroeffentlichten Versionen kontrollieren. Das Skript LAEDT HOCH (gh) - nur nach ausdruecklicher Freigabe ausfuehren.
# Duncan's Tidy und die Suite selbst haben eigene Wege (DownloadCleaner\Release.ps1 bzw. tools\ReleaseAll.ps1) und werden nur angezeigt.
param([switch]$Build, [switch]$DryRun, [switch]$Yes, [switch]$Force, [string[]]$Only = @())
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $root
$rel = Join-Path $repoRoot 'release'
$suiteRepo = 'Neox365/Duncans-Suite'; $pdfRepo = 'Neox365/Duncans-PDF-Reader'; $pdfTag = 'v1.0.0'

# Name fuer PublishModule.ps1, EXE-Datei, CMake-Ziel
$modules = @(
    [pscustomobject]@{ Name = 'DriverUpdater'; Exe = 'DuncansDriverUpdater.exe'; Target = 'DuncansDriverUpdater' }
    [pscustomobject]@{ Name = 'Privacy';       Exe = 'DuncansPrivacy.exe';       Target = 'DuncansPrivacyShield' }
    [pscustomobject]@{ Name = 'Photo';         Exe = 'DuncansPhotoCommander.exe'; Target = 'DuncansPhotoCommander' }
    [pscustomobject]@{ Name = 'Docs';          Exe = 'OggysDocsFiles.exe';       Target = 'OggysDocsFiles' }
    [pscustomobject]@{ Name = 'Vault';         Exe = 'DuncansVault.exe';         Target = 'DuncansVault' }
    [pscustomobject]@{ Name = 'Office';        Exe = 'DuncansOffice.exe';        Target = 'DuncansOffice' }
    [pscustomobject]@{ Name = 'QuickConvert';  Exe = 'DuncansQuickConvert.exe';  Target = 'DuncansQuickConvert' }
    [pscustomobject]@{ Name = 'Clip';          Exe = 'DuncansClip.exe';          Target = 'DuncansClip' }
    [pscustomobject]@{ Name = 'Boost';         Exe = 'DuncansBoost.exe';         Target = 'DuncansBoost' }
    [pscustomobject]@{ Name = 'Pdf';           Exe = 'DuncansPdf.exe';           Target = 'DuncansPdfReader' }
)
$Only = @($Only | ForEach-Object { $_ -split '[,;]' } | Where-Object { $_ })   # per "powershell -File" kommt "Vault,Pdf" als ein Text an
if ($Only.Count) { $modules = @($modules | Where-Object { $Only -contains $_.Name }) }
if (-not $modules.Count) { throw 'Kein Programm ausgewaehlt (-Only).' }

function Get-ExeVersion($exe) {
    $p = Join-Path $rel $exe
    if (-not (Test-Path $p)) { return $null }
    $v = (Get-Item $p).VersionInfo.FileVersion
    return [version](($v -split '\.')[0..2] -join '.')
}
function Read-Remote($repo, $tag, $pattern) {
    $tmp = Join-Path $env:TEMP "pc_$([guid]::NewGuid().ToString('N'))"; New-Item -ItemType Directory $tmp | Out-Null
    try {
        $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        gh release download $tag --pattern $pattern --dir $tmp --repo $repo 2>$null
        $ErrorActionPreference = $old
        $f = Join-Path $tmp $pattern
        if (Test-Path $f) { return (Get-Content $f -Raw) }
        return $null
    } finally { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }
}
# oberste Version im Abschnitt [exe] von changelog.txt
function Get-ChangelogTop($exe) {
    $c = Join-Path $repoRoot 'changelog.txt'
    $in = $false
    foreach ($line in Get-Content $c -Encoding UTF8) {
        if ($line -match '^\[(.+)\]\s*$') { $in = ($Matches[1] -eq $exe); continue }
        if ($in -and $line -match '^##\s*([0-9][0-9.]*)') { return [version]$Matches[1] }
    }
    return $null
}

if ($Build) {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmake) { throw 'cmake nicht gefunden.' }
    foreach ($m in $modules) {
        Write-Host "Baue $($m.Target) ..."
        & cmake --build (Join-Path $repoRoot 'build-release') --config Release --target $m.Target | Out-Null
        if ($LASTEXITCODE) { throw "Bauen fehlgeschlagen: $($m.Target) (laeuft das Programm noch? Dann erst beenden.)" }
    }
}

# veroeffentlichte Versionen
$remoteMods = Read-Remote $suiteRepo 'modules' 'modules.txt'
$remote = @{}
if ($remoteMods) { foreach ($l in ($remoteMods -split "`n")) { if ($l -match '^\s*(.+?\.exe)=([0-9.]+)') { $remote[$Matches[1]] = [version]$Matches[2] } } }
$pdfRemote = Read-Remote $pdfRepo $pdfTag 'version.txt'
if ($pdfRemote) { $remote['DuncansPdf.exe'] = [version]($pdfRemote.Trim()) }

$rows = @()
foreach ($m in $modules) {
    $local = Get-ExeVersion $m.Exe
    $pub = $remote[$m.Exe]
    $log = Get-ChangelogTop $m.Exe
    $state = if (-not $local) { 'fehlt in release\' }
             elseif (-not $pub) { 'neu (noch nie veroeffentlicht)' }
             elseif ($local -gt $pub) { 'NEU' }
             elseif ($local -eq $pub) { 'aktuell' }
             else { 'lokal AELTER als veroeffentlicht' }
    $logOk = ($state -ne 'NEU' -and $state -notlike 'neu*') -or ($log -and $local -and $log -ge $local)
    $rows += [pscustomobject]@{ Programm = $m.Name; Lokal = $local; Veroeffentlicht = $pub; Changelog = $log; Status = $state; Eintrag = $(if ($logOk) { 'ok' } else { 'FEHLT' }); Module = $m }
}
$rows | Format-Table Programm, Lokal, Veroeffentlicht, Changelog, Status, Eintrag -AutoSize | Out-String | Write-Host
Write-Host 'Nicht enthalten: Duncan''s Tidy (DownloadCleaner\Release.ps1) und die Suite (tools\ReleaseAll.ps1).'

$todo = @($rows | Where-Object { $_.Status -eq 'NEU' -or $_.Status -like 'neu*' })
$older = @($rows | Where-Object { $_.Status -like 'lokal AELTER*' })
if ($older.Count) { Write-Warning ("Lokal aelter als veroeffentlicht: " + (($older | ForEach-Object { $_.Programm }) -join ', ') + ' - vermutlich nicht neu gebaut (-Build).') }
if (-not $todo.Count) { Write-Host 'Nichts zu veroeffentlichen.'; return }
$noLog = @($todo | Where-Object { $_.Eintrag -ne 'ok' })
if ($noLog.Count -and -not $Force) { throw ('changelog.txt hat keinen Eintrag zur neuen Version fuer: ' + (($noLog | ForEach-Object { $_.Programm }) -join ', ') + ' (mit -Force trotzdem).') }
Write-Host ('Wird veroeffentlicht: ' + (($todo | ForEach-Object { "$($_.Programm) $($_.Lokal)" }) -join ', '))
if ($DryRun) { Write-Host '(-DryRun: nichts hochgeladen)'; return }
if (-not $Yes) { $a = Read-Host 'Jetzt hochladen? [j/N]'; if ($a -notmatch '^[jJyY]') { Write-Host 'Abgebrochen.'; return } }

$publish = Join-Path $root 'PublishModule.ps1'
foreach ($r in $todo) {
    Write-Host "== $($r.Programm)"
    & $publish -Module $r.Module.Name
}

# Kontrolle: stimmen die veroeffentlichten Versionen jetzt?
$remoteMods = Read-Remote $suiteRepo 'modules' 'modules.txt'
$pdfRemote = Read-Remote $pdfRepo $pdfTag 'version.txt'
$bad = @()
foreach ($r in $todo) {
    $got = $null
    if ($r.Module.Name -eq 'Pdf') { if ($pdfRemote) { $got = [version]($pdfRemote.Trim()) } }
    elseif ($remoteMods -and $remoteMods -match ('(?m)^' + [regex]::Escape($r.Module.Exe) + '=([0-9.]+)')) { $got = [version]$Matches[1] }
    if ($got -ne $r.Lokal) { $bad += $r.Programm }
}
if ($bad.Count) { Write-Warning ('Version online stimmt nicht: ' + ($bad -join ', ')) } else { Write-Host 'Alle veroeffentlichten Versionen stimmen.' }
