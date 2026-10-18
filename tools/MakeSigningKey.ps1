# Erzeugt (falls noetig) das Signatur-Schluesselpaar der Suite (ECDSA P-256) und schreibt den
# oeffentlichen Schluessel nach suite\public_key.inc (wird ins Programm einkompiliert).
#
# Der PRIVATE Schluessel liegt in  %USERPROFILE%\DuncansSuite-Signing\signing_private.blob
#   - NIE auf GitHub hochladen, NIE weitergeben, NIE ins Repository legen.
#   - Sichere ihn zusaetzlich (z. B. USB-Stick). Geht er verloren, nehmen bereits installierte Suiten
#     keine Updates mehr an und muessen einmal neu installiert werden.
# Dieser Schluessel ist getrennt von dem von Duncan's Tidy.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Core
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$dir  = Join-Path $env:USERPROFILE 'DuncansSuite-Signing'
$priv = Join-Path $dir 'signing_private.blob'
$inc  = Join-Path $root 'suite\public_key.inc'

if (-not (Test-Path $priv)) {
    New-Item -ItemType Directory -Force $dir | Out-Null
    $p = New-Object System.Security.Cryptography.CngKeyCreationParameters
    $p.ExportPolicy = [Security.Cryptography.CngExportPolicies]::AllowPlaintextExport
    $key = [Security.Cryptography.CngKey]::Create([Security.Cryptography.CngAlgorithm]::ECDsaP256, [NullString]::Value, $p)
    [IO.File]::WriteAllBytes($priv, $key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob))
    Write-Host "Neuer Signatur-Schluessel erstellt: $priv"
    Write-Host "WICHTIG: Sichere diese Datei zusaetzlich (z. B. USB-Stick) und gib sie niemandem."
}

$key = [Security.Cryptography.CngKey]::Import([IO.File]::ReadAllBytes($priv), [Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
$pub = $key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
$hex = ($pub | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', '
$text = "// Oeffentlicher Update-Schluessel der Suite (ECDSA P-256), erzeugt von tools\MakeSigningKey.ps1. Nicht von Hand aendern.`r`n" +
        "static const unsigned char kSuitePublicKey[] = { $hex };`r`n"
if (-not (Test-Path $inc) -or ((Get-Content $inc -Raw) -ne $text)) { [IO.File]::WriteAllText($inc, $text) }
Write-Host "Oeffentlicher Schluessel: $inc"
