# Signiert Release-Dateien mit dem privaten Suite-Schluessel und legt <Datei>.sig daneben (64 Byte, ECDSA P-256 / SHA-256).
# Aufruf:  .\tools\SignRelease.ps1 installer\Output\DuncansSuite_Setup_1.0.6.exe
#          .\tools\SignRelease.ps1 DuncansPdf.exe          (fuer das Release von Duncans-PDF-Reader)
# Danach die .sig-Datei als zusaetzliches Asset im selben GitHub-Release hochladen.
param([Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Files)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Core
$priv = Join-Path $env:USERPROFILE 'DuncansSuite-Signing\signing_private.blob'
if (-not (Test-Path $priv)) { throw "Privater Schluessel fehlt: $priv  (zuerst tools\MakeSigningKey.ps1 ausfuehren)" }
$key = [Security.Cryptography.CngKey]::Import([IO.File]::ReadAllBytes($priv), [Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
$ec = New-Object Security.Cryptography.ECDsaCng($key)
foreach ($f in $Files) {
    $path = (Resolve-Path $f).Path
    $sig = $ec.SignData([IO.File]::ReadAllBytes($path), [Security.Cryptography.HashAlgorithmName]::SHA256)
    [IO.File]::WriteAllBytes("$path.sig", $sig)
    Write-Host "Signiert: $path.sig ($($sig.Length) Byte)"
}
