# Duncan's Driver Updater

Hardware- & Treiber-Manager für Windows (C++ / Win32, Dark-UI). Liest alle Geräte samt Treiberdaten
aus, gruppiert sie, erkennt Updates und kann Treiber-Updates über Windows Update installieren.

## Funktionen

- **Geräte-Scan** über die Windows SetupAPI (Name, Kategorie, Version, Datum, Anbieter, Hardware-ID, Problemcode), asynchron im Hintergrund
- **Gruppierte Ansicht** (ausklappbar): Grafik & Monitor, Audio, Netzwerk & Bluetooth, Peripherie, Speicher, System & Prozessor, Sonstige
- **Update-Erkennung**
  - über den Windows Update Agent (Treiber-Updates, per Hardware-ID abgeglichen)
  - **Hersteller-Abgleich** für Grafikkarten (NVIDIA `VEN_10DE`, AMD `VEN_1002`, Intel `VEN_8086`) anhand von `vendor_releases.json`
- **Aktualisieren-Button** pro Karte: Windows-Update-Treiber werden mit Wiederherstellungspunkt installiert, bei Hersteller-Treibern öffnet sich die offizielle Download-Seite
- Suche, Filter (Alle / Updates / Probleme), Sortierung, CSV-Export, Treiber sichern (`pnputil /export-driver`), Hardware-ID kopieren
- Eigene Dark-Dialoge und Toasts, Banner mit Start-Animation

Die App benötigt Administratorrechte (UAC-Abfrage beim Start), weil Treiber-Operationen sie erfordern.

## Duncan's Suite (Launcher)

`DuncansSuite.exe` ist ein kleines Hauptmenü im selben Dark-Stil: zwei Karten starten **Duncan's Tidy** (`DuncansTidy.exe`)
und den **Driver Updater** (`DuncansDriverUpdater.exe`). Gesucht wird zuerst im Ordner der Suite, für Tidy zusätzlich in
`%LOCALAPPDATA%\DuncansTidy`. Ist Tidy nicht installiert, zeigt die Karte „Nicht installiert“ und den Button **Herunterladen**: Ein Klick lädt die neueste `DuncansTidy.exe` per HTTPS von den GitHub-Releases (`Neox365/Duncans-Tidy-ToolSet`) nach `%LOCALAPPDATA%\DuncansTidy`, prüft die SHA-256-Summe aus `DuncansTidy.exe.sha256` und schaltet den Button danach auf „Starten“. Fehlt eine andere EXE, erscheint ein Dark-Dialog mit den durchsuchten Pfaden. Der Footer zeigt die Version
und öffnet mit „Tools-Ordner öffnen“ den Installationsordner. Der Installer legt die Suite als Haupt-Verknüpfung auf den Desktop
und ins Startmenü.

### Automatische Updates der Suite

Beim Start fragt die Suite im Hintergrund die GitHub-Releases (`Neox365/Duncans-Suite`) ab. Gibt es einen neueren
`DuncansSuite_Setup_X.Y.Z.exe`, erscheint oben im Banner **„Update verfügbar (vX.Y.Z)“**. Ein Klick lädt den Installer in den
Temp-Ordner (mit Fortschritt), prüft die SHA-256-Summe (`digest` des GitHub-Assets), startet ihn (UAC) und beendet die Suite.
Es wird die Release-**Liste** abgefragt statt `/releases/latest`, weil `latest` Pre-Releases ignoriert. Ist man offline
oder GitHub nicht erreichbar, bleibt die Prüfung still.

## Hersteller-Tabelle (`vendor_releases.json`)

Die SetupAPI kennt nur den *installierten* Treiber. Welche Version der Hersteller zuletzt veröffentlicht hat,
steht in `resources/vendor_releases.json` (wird beim Build neben die EXE kopiert und vom Installer mitgeliefert):

```json
{ "vendor": "NVIDIA", "ven": "VEN_10DE", "name": "GeForce Game Ready Treiber 617.42",
  "version": "32.0.16.1742", "date": "2026-10-06", "url": "https://www.nvidia.com/en-us/geforce/drivers/" }
```

- `version` ist die **Windows-Treiberversion** (`a.b.c.d`, wie im Geräte-Manager). NVIDIA: Release `617.42` entspricht `32.0.16.1742`.
- Leere `version` = Eintrag inaktiv (AMD ist so vorbelegt, bis eine geprüfte Version eingetragen ist).
- Die Tabelle veraltet mit jedem neuen Hersteller-Release und muss gepflegt werden. Eine Datei neben der EXE überschreibt die eingebauten Werte.
- Verglichen wird nur innerhalb desselben Treiber-Hauptzweigs (z. B. `32.0.x`), um falschen Alarm bei älteren GPU-Generationen zu vermeiden.

## Bauen

Voraussetzungen: CMake ≥ 3.20 und ein C++17-Compiler (MinGW-w64 oder MSVC), Windows 10/11.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Die EXE landet im Projekt-Hauptordner (`DuncansDriverUpdater.exe`), `vendor_releases.json` wird daneben kopiert.
Mit MinGW wird statisch gelinkt, es sind keine zusätzlichen DLLs nötig.

## Installer

[Inno Setup 6](https://jrsoftware.org/isinfo.php) installieren, dann nach dem Build:

```powershell
ISCC.exe installer\installer.iss
```

Ergebnis: `installer\Output\DuncansSuite_Setup_1.0.3.exe` (Admin-Installer, Startmenü, optionale Desktop-Verknüpfung, Deinstaller).

## Projektstruktur

| Pfad | Inhalt |
| --- | --- |
| `src/HardwareScanner.*` | SetupAPI-Scan (asynchron) |
| `src/UpdateChecker.*`, `src/UpdateInstaller.*`, `src/Dispatch.h` | Windows Update Agent (Suche / Installation, COM-Automation) |
| `src/VendorReleases.*` | Hersteller-Abgleich der Grafiktreiber |
| `src/MainWindow.*`, `src/Icons.*` | UI (Win32 + GDI/GDI+), Banner, Splash, Dialoge |
| `suite/main.cpp` | Duncan's Suite (Launcher) |
| `resources/` | Icons, Manifeste (Driver Updater: requireAdministrator, Suite: asInvoker), Versionsinfo, `vendor_releases.json` |
| `installer/installer.iss` | Inno-Setup-Skript |

## Hinweise

- Die Installation von Windows-Update-Treibern wurde bisher nicht mit einem echten Update getestet.
- Es gibt keinen automatischen Download von Hersteller-Treibern; dafür wird die Download-Seite geöffnet.
