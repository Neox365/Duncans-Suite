# Laedt die vorkompilierte 64-Bit-pdfium.dll (bblanchon/pdfium-binaries) und legt sie nach pdf\third_party (CMake kopiert sie beim Build nach release\).
# Aufruf:  powershell -ExecutionPolicy Bypass -File pdf\third_party\get_pdfium.ps1
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$dest = $PSScriptRoot
$tmp  = Join-Path $env:TEMP ("pdfium_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $url = "https://github.com/bblanchon/pdfium-binaries/releases/latest/download/pdfium-win-x64.tgz"
    $tgz = Join-Path $tmp "pdfium-win-x64.tgz"
    Write-Host "Lade $url ..."
    Invoke-WebRequest -Uri $url -OutFile $tgz -UseBasicParsing
    tar -xzf $tgz -C $tmp
    Copy-Item (Join-Path $tmp "bin\pdfium.dll") $dest -Force
    if (Test-Path (Join-Path $tmp "LICENSE")) { Copy-Item (Join-Path $tmp "LICENSE") (Join-Path $root "pdf\third_party\PDFIUM_LICENSE.txt") -Force }
    Write-Host "pdfium.dll nach $dest kopiert."
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
