# Zeigt oder setzt die Version eines Programms an ALLEN Stellen (CMake, .rc numerisch + Text, main.cpp, installer.iss).
# Aufruf:
#   .\tools\Versions.ps1                              # prueft alle Programme, Exitcode 1 bei Abweichung
#   .\tools\Versions.ps1 -Module Office -Set 0.3.3    # setzt die Version von Office ueberall
# Module: DriverUpdater, Suite, Pdf, Photo, Docs, Vault, Office, QuickConvert, Privacy
param([ValidateSet('DriverUpdater', 'Suite', 'Pdf', 'Photo', 'Docs', 'Vault', 'Office', 'QuickConvert', 'Privacy')][string]$Module, [string]$Set)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$enc = [Text.Encoding]::GetEncoding('iso-8859-1')   # byteweise erhalten, egal ob UTF-8 oder ANSI

# Pro Programm: Datei + Regex, dessen Gruppe 1 die Version ist (Text-Stellen); die .rc wird separat behandelt (numerisch + Text)
$map = [ordered]@{
    DriverUpdater = @{ rc = 'app.rc';        refs = @(,@('CMakeLists.txt', 'project\(DuncansDriverUpdater VERSION ([\d.]+)')) }
    Suite         = @{ rc = 'suite.rc';      refs = @(@('CMakeLists.txt', 'set\(SUITE_VER "([\d.]+)"\)'), @('suite\main.cpp', '#define SUITE_VERSION L"([\d.]+)"'), @('installer\installer.iss', '#define MyAppVersion\s+"([\d.]+)"')) }
    Pdf           = @{ rc = 'pdf.rc';        refs = @(,@('CMakeLists.txt', 'set\(PDF_VER "([\d.]+)"\)')) }
    Photo         = @{ rc = 'photo.rc';      refs = @(,@('CMakeLists.txt', 'set\(PHOTO_VER "([\d.]+)"\)')) }
    Docs          = @{ rc = 'docs.rc';       refs = @(,@('CMakeLists.txt', 'set\(DOCS_VER "([\d.]+)"\)')) }
    Vault         = @{ rc = 'vault.rc';      refs = @(,@('CMakeLists.txt', 'set\(VAULT_VER "([\d.]+)"\)')) }
    Office        = @{ rc = 'office.rc';     refs = @(,@('CMakeLists.txt', 'set\(OFFICE_VER "([\d.]+)"\)')) }
    QuickConvert  = @{ rc = 'quickconvert.rc'; refs = @(,@('CMakeLists.txt', 'set\(QUICKCONVERT_VER "([\d.]+)"\)')) }
    Privacy       = @{ rc = 'privacy.rc';    refs = @(@('privacy\main.cpp', '#define APP_VERSION L"([\d.]+)"'), @('CMakeLists.txt', 'APP_VERSION=L\\"([\d.]+)\\"\)\s*$')) }
}

function ReadAll($rel) { $enc.GetString([IO.File]::ReadAllBytes((Join-Path $root $rel))) }
function WriteAll($rel, $t) { [IO.File]::WriteAllBytes((Join-Path $root $rel), $enc.GetBytes($t)) }
function Norm($v) { $p = @($v -split '\.'); while ($p.Count -lt 3) { $p += '0' }; ($p[0..2] -join '.') }

# Alle Versionen eines Programms einsammeln: @{ 'Datei: Stelle' = '1.2.3' }
function Collect($name) {
    $e = $map[$name]; $r = [ordered]@{}
    foreach ($ref in $e.refs) {
        $t = ReadAll $ref[0]
        # Privacy: die CMake-Zeile mit APP_VERSION=L\"x.y.z\" gehoert zu Privacy (die anderen nutzen ${...}); nimm die Zeile direkt nach dem Privacy-Ziel
        if ($name -eq 'Privacy' -and $ref[0] -eq 'CMakeLists.txt') {
            $m = [regex]::Match($t, '(?s)add_executable\(DuncansPrivacy.*?APP_VERSION=L\\"([\d.]+)\\"'); if ($m.Success) { $r["$($ref[0])"] = $m.Groups[1].Value }; continue
        }
        $m = [regex]::Match($t, $ref[1], 'Multiline'); $r["$($ref[0])"] = if ($m.Success) { $m.Groups[1].Value } else { '?' }
    }
    $rc = ReadAll "resources\$($e.rc)"
    $fv = [regex]::Match($rc, 'VALUE "FileVersion",\s*"([\d.]+)"').Groups[1].Value
    $pv = [regex]::Match($rc, 'VALUE "ProductVersion",\s*"([\d.]+)"').Groups[1].Value
    $nf = [regex]::Match($rc, 'FILEVERSION\s+(\d+),(\d+),(\d+)'); $np = [regex]::Match($rc, 'PRODUCTVERSION\s+(\d+),(\d+),(\d+)')
    $r["$($e.rc) FileVersion"] = $fv; $r["$($e.rc) ProductVersion"] = $pv
    $r["$($e.rc) FILEVERSION"] = "$($nf.Groups[1].Value).$($nf.Groups[2].Value).$($nf.Groups[3].Value)"
    $r["$($e.rc) PRODUCTVERSION"] = "$($np.Groups[1].Value).$($np.Groups[2].Value).$($np.Groups[3].Value)"
    $r
}

if ($Set) {
    if (-not $Module) { throw '-Set braucht -Module' }
    if ($Set -notmatch '^\d+\.\d+\.\d+$') { throw 'Version muss a.b.c sein' }
    $e = $map[$Module]; $v = $Set; $c = $v -split '\.'
    foreach ($ref in $e.refs) {
        $t = ReadAll $ref[0]
        if ($Module -eq 'Privacy' -and $ref[0] -eq 'CMakeLists.txt') {
            $t = [regex]::Replace($t, '(?s)(add_executable\(DuncansPrivacy.*?APP_VERSION=L\\")[\d.]+(\\")', "`${1}$v`${2}", 'None')
        } else {
            $m = [regex]::Match($t, $ref[1], 'Multiline'); if (-not $m.Success) { throw "Stelle nicht gefunden: $($ref[0])" }
            $g = $m.Groups[1]; $t = $t.Substring(0, $g.Index) + $v + $t.Substring($g.Index + $g.Length)
        }
        WriteAll $ref[0] $t
    }
    $rcRel = "resources\$($e.rc)"; $rc = ReadAll $rcRel
    $rc = [regex]::Replace($rc, '(FILEVERSION\s+)\d+,\d+,\d+,\d+', "`${1}$($c[0]),$($c[1]),$($c[2]),0")
    $rc = [regex]::Replace($rc, '(PRODUCTVERSION\s+)\d+,\d+,\d+,\d+', "`${1}$($c[0]),$($c[1]),$($c[2]),0")
    $rc = [regex]::Replace($rc, '(VALUE "FileVersion",\s*")[\d.]+(")', "`${1}$v`${2}")
    $rc = [regex]::Replace($rc, '(VALUE "ProductVersion",\s*")[\d.]+(")', "`${1}$v`${2}")
    WriteAll $rcRel $rc
    Write-Host "$Module -> $v gesetzt. Changelog (changelog.txt) nicht vergessen." -ForegroundColor Green
}

$bad = 0
foreach ($name in $(if ($Module) { @($Module) } else { $map.Keys })) {
    $vals = Collect $name
    $uniq = @($vals.Values | ForEach-Object { Norm $_ } | Sort-Object -Unique)
    $ok = ($uniq.Count -eq 1 -and $uniq[0] -notmatch '\?')
    if ($ok) { Write-Host ("{0,-13} {1}  OK" -f $name, $uniq[0]) -ForegroundColor Green }
    else { $bad++; Write-Host ("{0,-13} ABWEICHUNG" -f $name) -ForegroundColor Red; $vals.GetEnumerator() | ForEach-Object { Write-Host ("    {0,-32} {1}" -f $_.Key, $_.Value) } }
}
if ($bad) { exit 1 }
