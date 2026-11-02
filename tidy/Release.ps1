# Baut eine neue Version, signiert sie und legt die Dateien fuer das GitHub-Release in .\release ab.
# Aufruf:  .\Release.ps1 -Version 0.5.7 -Notes "Neu: ...","Fix: ..."   (mit -Publish wird das Release gleich auf GitHub veroeffentlicht, braucht die GitHub-CLI gh)
# Bei Aufruf per  powershell -File  die Notizen mit | trennen:  -Notes "Neu: ... | Fix: ..."
# Danach auf GitHub ein neues Release mit Tag v<Version> anlegen und diese 5 Dateien hochladen:
#   DuncansTidy.exe, DuncansTidy.exe.sha256, DuncansTidy.exe.sig, version.txt, changelog.txt
param([Parameter(Mandatory)][string]$Version, [string[]]$Notes = @(), [switch]$Publish)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vc = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat" | Select-Object -First 1 -ExpandProperty FullName
if (-not $vc) { throw 'Visual Studio (vcvars64.bat) nicht gefunden.' }

# Signatur-Schluessel sicherstellen (erzeugt public_key.inc, das ins Programm einkompiliert wird)
& (Join-Path $root 'tools\MakeSigningKey.ps1')

# Versionsnummer in Quellcode und version.rc eintragen
$cpp = Join-Path $root 'src\Download-Cleaner.cpp'
(Get-Content $cpp -Raw -Encoding UTF8) -replace 'kVersion\[\] = L"[^"]*"', "kVersion[] = L`"$Version`"" |
    Set-Content $cpp -Encoding UTF8
$four = ($Version + '.0.0.0').Split('.')[0..3] -join '.'
$rc = Join-Path $root 'src\version.rc'
(Get-Content $rc -Raw) -replace '(FILEVERSION|PRODUCTVERSION) [\d,]+', ('$1 ' + ($four -replace '\.', ',')) `
    -replace '("(File|Product)Version", ")[\d.]+', ('${1}' + $four) | Set-Content $rc -Encoding ASCII

$out = Join-Path $root 'release'
New-Item -ItemType Directory -Force $out | Out-Null
cmd /c "`"$vc`" >nul 2>&1 && cd /d `"$root\src`" && rc /nologo version.rc && cl /nologo /EHsc /std:c++17 /O2 /MT /utf-8 Download-Cleaner.cpp version.res /Fe:..\release\DuncansTidy.exe /Fo:..\release\\ /link /MANIFEST:EMBED /DEPENDENTLOADFLAG:0x800"
Remove-Item "$out\*.obj", "$out\*.manifest", "$root\src\version.res" -ErrorAction SilentlyContinue
if (-not (Test-Path "$out\DuncansTidy.exe")) { throw 'Build fehlgeschlagen.' }

$hash = (Get-FileHash "$out\DuncansTidy.exe" -Algorithm SHA256).Hash.ToLower()
Set-Content "$out\DuncansTidy.exe.sha256" $hash -NoNewline -Encoding ASCII
Set-Content "$out\version.txt" $Version -NoNewline -Encoding ASCII
$Notes = @($Notes | ForEach-Object { $_ -split '\s*\|\s*' } | Where-Object { $_ })  # Notizen duerfen auch mit | getrennt uebergeben werden
$lines = $Notes | ForEach-Object { "- $_" }
[IO.File]::WriteAllText("$out\changelog.txt", ($lines -join "`n"), (New-Object Text.UTF8Encoding $false))

# .exe mit dem privaten Schluessel signieren (ECDSA P-256 / SHA-256, 64 Byte)
Add-Type -AssemblyName System.Core
$priv = Join-Path $env:USERPROFILE 'DuncansTidy-Signing\signing_private.blob'
$key = [Security.Cryptography.CngKey]::Import([IO.File]::ReadAllBytes($priv), [Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
$ec = New-Object Security.Cryptography.ECDsaCng($key)
$sig = $ec.SignData([IO.File]::ReadAllBytes("$out\DuncansTidy.exe"), [Security.Cryptography.HashAlgorithmName]::SHA256)
[IO.File]::WriteAllBytes("$out\DuncansTidy.exe.sig", $sig)

# Installations-Assistent (Inno Setup, optional: nur wenn installiert, z. B. winget install JRSoftware.InnoSetup)
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if ($iscc) { & $iscc "/DAppVersion=$Version" (Join-Path $root 'installer\DuncansTidy.iss') | Out-Null }
else { Write-Host 'Hinweis: Inno Setup nicht gefunden, kein Installer gebaut.' }
Write-Host "Fertig. Dateien in $out :"; Get-ChildItem $out | Select-Object Name, Length

# Optional: Release gleich auf GitHub veroeffentlichen (einmalig: winget install GitHub.cli, danach gh auth login)
if ($Publish) {
    $repo = 'Neox365/Duncans-Tidy-ToolSet'
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'GitHub-CLI (gh) nicht gefunden. Installieren mit: winget install GitHub.cli' }
    gh auth status 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Nicht bei GitHub angemeldet. Einmalig ausfuehren: gh auth login' }
    $setup = "DuncansTidy-Setup-$Version.exe"
    $files = @('DuncansTidy.exe', 'DuncansTidy.exe.sha256', 'DuncansTidy.exe.sig', 'version.txt', 'changelog.txt', $setup) | ForEach-Object { Join-Path $out $_ }
    foreach ($f in $files) { if (-not (Test-Path $f)) { throw "Datei fehlt: $f" } }
    $body = "## Neu in v$Version`n`n" + ($lines -join "`n") + "`n`n## Installation`n" +
        "- **Neu hier?** ``$setup`` herunterladen und den Assistenten durchklicken. Keine Administrator-Rechte noetig.`n" +
        "- **Schon installiert?** Das Programm aktualisiert sich selbst: Update-Button oben rechts.`n" +
        "- Windows zeigt evtl. eine SmartScreen-Warnung (Weitere Informationen -> Trotzdem ausfuehren), weil die Datei nicht mit einem Zertifikat signiert ist. Updates im Programm werden zusaetzlich per Signatur geprueft.`n"
    $notesFile = Join-Path $env:TEMP "tidy_release_notes_$Version.md"
    [IO.File]::WriteAllText($notesFile, $body, (New-Object Text.UTF8Encoding $false))
    gh release create "v$Version" @files --repo $repo --title "v$Version" --notes-file $notesFile --latest
    if ($LASTEXITCODE -ne 0) { throw 'Veroeffentlichen auf GitHub fehlgeschlagen.' }
    Remove-Item $notesFile -ErrorAction SilentlyContinue
    Write-Host "Veroeffentlicht: https://github.com/$repo/releases/tag/v$Version"
}
