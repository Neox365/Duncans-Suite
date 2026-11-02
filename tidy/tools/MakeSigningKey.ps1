# Erzeugt (falls noetig) das Signatur-Schluesselpaar fuer Updates (ECDSA P-256) und schreibt den
# oeffentlichen Schluessel nach public_key.inc (wird ins Programm einkompiliert).
#
# Der PRIVATE Schluessel liegt in  %USERPROFILE%\DuncansTidy-Signing\signing_private.blob
#   - NIE auf GitHub hochladen, NIE weitergeben.
#   - Sichere ihn zusaetzlich (z.B. USB-Stick). Geht er verloren, koennen bestehende Installationen
#     keine Updates mehr annehmen und muessen einmal neu installiert werden.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Core
$root = Join-Path (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)) 'src'
$dir  = Join-Path $env:USERPROFILE 'DuncansTidy-Signing'
$priv = Join-Path $dir 'signing_private.blob'
$inc  = Join-Path $root 'public_key.inc'
$C = 'System.Security.Cryptography'

if (-not (Test-Path $priv)) {
    New-Item -ItemType Directory -Force $dir | Out-Null
    $p = New-Object "$C.CngKeyCreationParameters"
    $p.ExportPolicy = [Security.Cryptography.CngExportPolicies]::AllowPlaintextExport
    $key = [Security.Cryptography.CngKey]::Create([Security.Cryptography.CngAlgorithm]::ECDsaP256, $null, $p)
    [IO.File]::WriteAllBytes($priv, $key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob))
    Write-Host "Neuer Signatur-Schluessel erstellt: $priv"
    Write-Host "WICHTIG: Sichere diese Datei zusaetzlich (z.B. USB-Stick) und gib sie niemandem."
}

$key = [Security.Cryptography.CngKey]::Import([IO.File]::ReadAllBytes($priv), [Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
$pub = $key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
$hex = ($pub | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', '
$text = "// Oeffentlicher Update-Schluessel (ECDSA P-256), erzeugt von MakeSigningKey.ps1. Nicht von Hand aendern.`r`n" +
        "static const unsigned char kUpdatePublicKey[] = { $hex };`r`n"
if (-not (Test-Path $inc) -or ((Get-Content $inc -Raw) -ne $text)) { [IO.File]::WriteAllText($inc, $text) }

