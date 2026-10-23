# Baut, signiert und veroeffentlicht alles fuer ein Suite-Release in einem Rutsch (auf DEINEM PC mit dem privaten Schluessel):
#   1. Alle Programme bauen (Release, x64)           2. Module signieren (modules.txt, version.txt, .sig)
#   3. Installer bauen (schlank + Komplett) + signieren
#   4. Photo Commander und Oggys Docs-Files ins Release "modules" hochladen (PublishModule.ps1 -Module Photo / Docs)
#   5. Suite-Release "v<Version>" mit beiden Installern und Signaturen anlegen (die Suite erkennt es als Update)
# Voraussetzungen: cmake + Visual Studio (C++), Inno Setup 6 (ISCC.exe), GitHub-CLI "gh" (angemeldet), Signaturschluessel
# (%USERPROFILE%\DuncansSuite-Signing\signing_private.blob), pdfium.dll (pdf\third_party\get_pdfium.ps1).
# Aufruf:  .\tools\ReleaseAll.ps1            (fragt vor dem Hochladen nach)
#          .\tools\ReleaseAll.ps1 -DryRun    (baut und signiert nur, laedt nichts hoch)
#          .\tools\ReleaseAll.ps1 -Yes       (ohne Rueckfrage hochladen)
param([switch]$DryRun, [switch]$Yes, [string]$Notes = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$tools = Join-Path $root 'tools'
$rel = Join-Path $root 'release'
Set-Location $root

function Step($t) { Write-Host "`n=== $t ===" -ForegroundColor Cyan }
function Run($exe, [string[]]$a) { & $exe @a; if ($LASTEXITCODE) { throw "$exe fehlgeschlagen (Exitcode $LASTEXITCODE)" } }

Step 'Voraussetzungen pruefen'
$ver = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'set\(SUITE_VER "([\d.]+)"\)').Groups[1].Value
if (-not $ver) { throw 'SUITE_VER in CMakeLists.txt nicht gefunden' }
$tag = "v$ver"
Write-Host "Suite-Version: $ver (Tag $tag)"
if (-not (Test-Path (Join-Path $env:USERPROFILE 'DuncansSuite-Signing\signing_private.blob'))) { throw 'Privater Schluessel fehlt (tools\MakeSigningKey.ps1)' }
foreach ($c in 'cmake', 'gh') { if (-not (Get-Command $c -ErrorAction SilentlyContinue)) { throw "$c nicht gefunden" } }
$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) { $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1 }
if (-not $iscc) { throw 'Inno Setup 6 (ISCC.exe) nicht gefunden' }
$branch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') { Write-Warning "Du bist auf '$branch', nicht auf 'main'." }
if (git status --porcelain) { Write-Warning 'Es gibt nicht eingecheckte Aenderungen - sie landen im Build.' }
if (-not $DryRun) {
    $ErrorActionPreference = 'Continue'; gh release view $tag --repo Neox365/Duncans-Suite *> $null; $exists = ($LASTEXITCODE -eq 0); $ErrorActionPreference = 'Stop'
    if ($exists) { throw "Release $tag existiert schon. Version in CMakeLists.txt/suite/main.cpp/installer.iss/suite.rc erhoehen." }
}

Step 'pdfium.dll'
if (-not (Test-Path (Join-Path $root 'pdf\third_party\pdfium.dll'))) { & (Join-Path $root 'pdf\third_party\get_pdfium.ps1') }
if (-not (Test-Path (Join-Path $root 'pdf\third_party\pdfium.dll'))) { throw 'pdfium.dll fehlt' }

Step 'Bauen (Release, x64)'
$buildDir = Join-Path $root 'build-release'   # eigener Ordner, damit eine andere Konfiguration in 'build' nicht stoert
Run 'cmake' @('-S', $root, '-B', $buildDir, '-A', 'x64')
Run 'cmake' @('--build', $buildDir, '--config', 'Release')
foreach ($f in 'DuncansSuite.exe', 'DuncansDriverUpdater.exe', 'DuncansPdf.exe', 'DuncansPrivacy.exe', 'DuncansPhotoCommander.exe', 'OggysDocsFiles.exe', 'DuncansVault.exe', 'DuncansOffice.exe', 'pdfium.dll') {
    if (-not (Test-Path (Join-Path $rel $f))) { throw "release\$f fehlt nach dem Build" }
}

Step 'Module signieren'
& (Join-Path $tools 'ModuleAssets.ps1')
& (Join-Path $tools 'SignRelease.ps1') (Join-Path $rel 'DuncansSuite.exe') (Join-Path $rel 'DuncansPhotoCommander.exe') (Join-Path $rel 'OggysDocsFiles.exe') (Join-Path $rel 'DuncansVault.exe') (Join-Path $rel 'DuncansOffice.exe')

Step 'Installer bauen und signieren'
Run $iscc @((Join-Path $root 'installer\installer.iss'))
Run $iscc @('/DFull=1', (Join-Path $root 'installer\installer.iss'))
$slim = Join-Path $rel "DuncansSuite_Setup_$ver.exe"
$full = Join-Path $rel "DuncansSuite_Full_Setup_$ver.exe"
foreach ($f in $slim, $full) { if (-not (Test-Path $f)) { throw "$f fehlt (ISCC-Ausgabe pruefen)" } }
& (Join-Path $tools 'SignRelease.ps1') $slim $full

Step 'Ergebnis'
Get-ChildItem $rel -Filter '*.exe*' | Select-Object Name, Length | Format-Table -AutoSize
if ($DryRun) { Write-Host 'DryRun: nichts hochgeladen.' -ForegroundColor Yellow; return }

if (-not $Yes) {
    $a = Read-Host "Photo Commander und Oggys Docs-Files ins Release 'modules' und Suite $tag als neues Release auf GitHub hochladen? (j/N)"
    if ($a -notmatch '^[jJyY]') { Write-Host 'Abgebrochen.'; return }
}

Step "Photo Commander -> Release 'modules'"
& (Join-Path $tools 'PublishModule.ps1') -Module Photo
Step "Oggys Docs-Files -> Release 'modules'"
& (Join-Path $tools 'PublishModule.ps1') -Module Docs
Step "Duncan's Vault -> Release 'modules'"
& (Join-Path $tools 'PublishModule.ps1') -Module Vault
Step "Duncan's Office -> Release 'modules'"
& (Join-Path $tools 'PublishModule.ps1') -Module Office

Step "Suite $tag veroeffentlichen"
$notesText = if ($Notes) { $Notes } else { "Duncan's Suite $ver" }
$assets = @($slim, "$slim.sig", $full, "$full.sig")
Run 'gh' (@('release', 'create', $tag, '--repo', 'Neox365/Duncans-Suite', '--title', "Duncan's Suite $ver", '--notes', $notesText) + $assets)
Write-Host "`nFertig: Suite $tag ist veroeffentlicht. Bestehende Suiten bieten das Update beim naechsten Start an." -ForegroundColor Green
