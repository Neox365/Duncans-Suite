# Signiert resources\vendor_releases.json fuer den Online-Abgleich des Driver Updaters (legt vendor_releases.json.sig daneben).
# Aufruf:  .\tools\SignVendorTable.ps1
# Danach beide Dateien committen und pushen; der Driver Updater holt sie beim Start von GitHub (raw) und prueft die Signatur.
# Die Datei wird vorher auf LF-Zeilenenden und UTF-8 ohne BOM gebracht, weil GitHub genau diese Bytes ausliefert.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$file = Join-Path $root 'resources\vendor_releases.json'
$text = [IO.File]::ReadAllText($file) -replace "`r`n", "`n"
if ($text.StartsWith([string][char]0xFEFF)) { $text = $text.Substring(1) }
[IO.File]::WriteAllText($file, $text, (New-Object Text.UTF8Encoding $false))
& (Join-Path $PSScriptRoot 'SignRelease.ps1') $file
