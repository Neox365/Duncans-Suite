# Setzt die Version eines Programms an ALLEN Stellen (CMake, .rc numerisch + Text, main.cpp, installer.iss) und prueft danach mit CheckVersions.ps1.
# Aufruf:  .\tools\Versions.ps1 -Module Office -Set 0.3.3   (changelog.txt weiter von Hand ergaenzen)
# Module: DriverUpdater, Suite, Pdf, Photo, Docs, Vault, Office, QuickConvert, Privacy, Boost, Clip, Equalizer
param([Parameter(Mandatory)][ValidateSet('DriverUpdater', 'Suite', 'Pdf', 'Photo', 'Docs', 'Vault', 'Office', 'QuickConvert', 'Privacy', 'Boost', 'Clip', 'Equalizer')][string]$Module, [Parameter(Mandatory)][string]$Set)
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
    Clip          = @{ rc = 'clip.rc';       refs = @(,@('CMakeLists.txt', 'set\(CLIP_VER "([\d.]+)"\)')) }
    Equalizer     = @{ rc = 'equalizer.rc';  refs = @(,@('CMakeLists.txt', 'set\(EQUALIZER_VER "([\d.]+)"\)')) }
    Boost         = @{ rc = 'boost.rc';      refs = @(,@('CMakeLists.txt', 'set\(BOOST_VER "([\d.]+)"\)')) }
    Privacy       = @{ rc = 'privacy.rc';    refs = @(@('privacy\main.cpp', '#define APP_VERSION L"([\d.]+)"'), @('CMakeLists.txt', 'APP_VERSION=L\\"([\d.]+)\\"\)\s*$')) }
}

function ReadAll($rel) { $enc.GetString([IO.File]::ReadAllBytes((Join-Path $root $rel))) }
function WriteAll($rel, $t) { [IO.File]::WriteAllBytes((Join-Path $root $rel), $enc.GetBytes($t)) }
function Norm($v) { $p = @($v -split '\.'); while ($p.Count -lt 3) { $p += '0' }; ($p[0..2] -join '.') }

if ($Set) {
    
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

& (Join-Path $PSScriptRoot 'CheckVersions.ps1')
