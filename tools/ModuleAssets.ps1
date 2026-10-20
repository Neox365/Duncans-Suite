# Bereitet die Dateien fuer die zentrale Update-Pruefung der Suite vor (vor jedem Suite-Release ausfuehren, nach dem Bauen):
#   release\modules.txt                      Zeilen "DuncansDriverUpdater.exe=1.1.22" / "DuncansPrivacy.exe=0.6.0" / "DuncansPhotoCommander.exe=0.1.0" (Versionen aus den EXE-Dateien)
#   release\DuncansDriverUpdater.exe.sig     Signatur (Suite-Schluessel), wird von der Suite beim Download geprueft
#   release\DuncansPrivacy.exe.sig
#   release\version.txt                      Version des PDF Readers (fuer das Release in Neox365/Duncans-PDF-Reader)
# Hochladen: Suite-Release: DuncansDriverUpdater.exe(+.sig), DuncansPrivacy.exe(+.sig), modules.txt  |  PDF-Release: DuncansPdf.exe(+.sig), version.txt
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$rel = Join-Path (Split-Path -Parent $root) 'release'
function Ver($f) { $v = (Get-Item (Join-Path $rel $f)).VersionInfo.FileVersion; ($v -split '\.')[0..2] -join '.' }
$lines = @("DuncansDriverUpdater.exe=$(Ver 'DuncansDriverUpdater.exe')", "DuncansPrivacy.exe=$(Ver 'DuncansPrivacy.exe')", "DuncansPhotoCommander.exe=$(Ver 'DuncansPhotoCommander.exe')", "OggysDocsFiles.exe=$(Ver 'OggysDocsFiles.exe')")
[IO.File]::WriteAllText((Join-Path $rel 'modules.txt'), ($lines -join "`n"), (New-Object Text.UTF8Encoding $false))
[IO.File]::WriteAllText((Join-Path $rel 'version.txt'), (Ver 'DuncansPdf.exe'), (New-Object Text.UTF8Encoding $false))
& (Join-Path $root 'SignRelease.ps1') (Join-Path $rel 'DuncansDriverUpdater.exe') (Join-Path $rel 'DuncansPrivacy.exe') (Join-Path $rel 'DuncansPhotoCommander.exe') (Join-Path $rel 'OggysDocsFiles.exe') (Join-Path $rel 'DuncansPdf.exe') (Join-Path $rel 'pdfium.dll')
Write-Host "modules.txt:"; Get-Content (Join-Path $rel 'modules.txt')
Write-Host "version.txt (PDF): $(Get-Content (Join-Path $rel 'version.txt'))"
