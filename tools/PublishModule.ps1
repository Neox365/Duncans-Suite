# Veroeffentlicht EIN Programm einzeln, ohne neue Suite-Version (die Suite bietet das Update auf der jeweiligen Karte an).
# Aufruf (nach dem Bauen, Version der EXE vorher angehoben):
#   .\tools\PublishModule.ps1 -Module DriverUpdater      # release\DuncansDriverUpdater.exe -> Release "modules" im Suite-Repo
#   .\tools\PublishModule.ps1 -Module Photo            # release\DuncansPhotoCommander.exe -> Release "modules" im Suite-Repo
#   .\tools\PublishModule.ps1 -Module Docs             # release\OggysDocsFiles.exe       -> Release "modules" im Suite-Repo
#   .\tools\PublishModule.ps1 -Module Vault            # release\DuncansVault.exe        -> Release "modules" im Suite-Repo
#   .\tools\PublishModule.ps1 -Module QuickConvert   # release\DuncansQuickConvert.exe -> Release "modules" im Suite-Repo
#   .\tools\PublishModule.ps1 -Module Office           # release\DuncansOffice.exe       -> Release "modules" im Suite-Repo
#   .\tools\PublishModule.ps1 -Module Privacy            # release\DuncansPrivacy.exe       -> Release "modules" im Suite-Repo
#   .\tools\PublishModule.ps1 -Module Pdf                # release\DuncansPdf.exe (+ pdfium.dll) -> Release v1.0.0 in Neox365/Duncans-PDF-Reader
# Tidy hat sein eigenes Release.ps1 (DownloadCleaner). Das Skript LAEDT HOCH (gh) - nur nach ausdruecklicher Freigabe ausfuehren.
param([Parameter(Mandatory)][ValidateSet('DriverUpdater', 'Privacy', 'Photo', 'Docs', 'Vault', 'Office', 'QuickConvert', 'Pdf')][string]$Module, [switch]$DryRun)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$rel = Join-Path (Split-Path -Parent $root) 'release'
function FileVer($f) { $v = (Get-Item (Join-Path $rel $f)).VersionInfo.FileVersion; ($v -split '\.')[0..2] -join '.' }
$sign = Join-Path $root 'SignRelease.ps1'
# changelog.txt (Neuigkeiten im Info-Dialog der Suite) liegt im Release "modules" des Suite-Repos und wird bei jedem Programm-Release mit hochgeladen
$changelog = Join-Path (Split-Path -Parent $root) 'changelog.txt'

if ($Module -eq 'Pdf') {
    $repo = 'Neox365/Duncans-PDF-Reader'; $tag = 'v1.0.0'
    $ver = FileVer 'DuncansPdf.exe'
    [IO.File]::WriteAllText((Join-Path $rel 'version.txt'), $ver, (New-Object Text.UTF8Encoding $false))
    & $sign (Join-Path $rel 'DuncansPdf.exe') (Join-Path $rel 'pdfium.dll')
    $files = @('DuncansPdf.exe', 'DuncansPdf.exe.sig', 'pdfium.dll', 'pdfium.dll.sig', 'version.txt') | ForEach-Object { Join-Path $rel $_ }
    Write-Host "PDF Reader $ver -> $repo $tag"
    if (-not $DryRun) {
        gh release upload $tag @files --clobber --repo $repo; if ($LASTEXITCODE) { throw 'Upload fehlgeschlagen' }
        $ErrorActionPreference = 'Continue'; gh release upload modules $changelog --clobber --repo Neox365/Duncans-Suite; if ($LASTEXITCODE) { Write-Warning 'changelog.txt konnte nicht ins Release modules hochgeladen werden.' }; $ErrorActionPreference = 'Stop'
    }
    return
}

$repo = 'Neox365/Duncans-Suite'; $tag = 'modules'
$exe = switch ($Module) { 'DriverUpdater' { 'DuncansDriverUpdater.exe' } 'Photo' { 'DuncansPhotoCommander.exe' } 'Docs' { 'OggysDocsFiles.exe' } 'Vault' { 'DuncansVault.exe' } 'Office' { 'DuncansOffice.exe' } 'QuickConvert' { 'DuncansQuickConvert.exe' } default { 'DuncansPrivacy.exe' } }
$ver = FileVer $exe
& $sign (Join-Path $rel $exe)

# modules.txt aus dem Release lesen und nur die Zeile dieses Programms ersetzen (das andere Programm bleibt unveraendert)
$tmp = Join-Path $env:TEMP "modules_$([guid]::NewGuid().ToString('N'))"; New-Item -ItemType Directory $tmp | Out-Null
$ErrorActionPreference = 'Continue'; gh release download $tag --pattern modules.txt --dir $tmp --repo $repo 2>$null; $ErrorActionPreference = 'Stop'
$lines = @()
if (Test-Path (Join-Path $tmp 'modules.txt')) { $lines = @(Get-Content (Join-Path $tmp 'modules.txt') | Where-Object { $_ -and ($_ -notlike "$exe=*") }) }
$lines += "$exe=$ver"
$mod = Join-Path $rel 'modules.txt'
[IO.File]::WriteAllText($mod, ($lines -join "`n"), (New-Object Text.UTF8Encoding $false))
Remove-Item $tmp -Recurse -Force
Write-Host "$exe $ver -> $repo ($tag)"; Get-Content $mod

if ($DryRun) { return }
# Release "modules" einmalig als Vorab-Version anlegen (zaehlt dann nicht als "latest" der Suite)
$ErrorActionPreference = 'Continue'; gh release view $tag --repo $repo *> $null; $viewRc = $LASTEXITCODE; $ErrorActionPreference = 'Stop'
if ($viewRc) { gh release create $tag --repo $repo --prerelease --title "Programm-Updates (modules)" --notes "Laufende Programm-Dateien fuer die Update-Pruefung der Suite. Nicht manuell herunterladen: Installer stehen in den Suite-Releases." }
gh release upload $tag (Join-Path $rel $exe) (Join-Path $rel "$exe.sig") $mod $changelog --clobber --repo $repo
if ($LASTEXITCODE) { throw 'Upload fehlgeschlagen' }
