# Duncan's Suite

Windows-Programmsammlung in C++ / Win32 mit einheitlichem Dark-UI, Sprache der Programme: Deutsch.

| Programm | Aufgabe |
| --- | --- |
| **Duncan's Suite** | Launcher: startet die Programme, lädt fehlende per Ein-Klick-Download und aktualisiert sich selbst |
| **Duncan's Driver Updater** | Hardware- und Treiber-Manager (Scan, Update-Erkennung, Installation über Windows Update) |
| **Duncan's PDF Reader** | PDF-Betrachter und -Werkzeug (PDFium) |
| **Duncan's Photo Commander** | Foto-Verwaltung (Ordnerbaum, asynchrones Thumbnail-Grid, EXIF-/GPS-Cleaner), Quellen in `photo/` – Grundgerüst |
| **Duncan's Tidy** | System-Cleaner und Optimierer, eigenes Projekt ([Neox365/Duncans-Tidy-ToolSet](https://github.com/Neox365/Duncans-Tidy-ToolSet)); die Suite lädt es nur herunter und startet es |

## Duncan's Photo Commander (Grundgerüst)

Win32 + Direct2D/DirectWrite + WIC in `photo/`, Ziel `DuncansPhotoCommander.exe` (Farben aus `common/Theme.h` über `photo/Theme.h`).
Topbar (Ordner öffnen, Vollbild, Stapelverarbeitung, Zahnrad), links Ordnerbaum, Mitte Thumbnail-Grid mit asynchronem WIC-Laden,
rechts Detail-Panel (EXIF, Auflösung, Dateigröße) mit Schnellaktionen (Drehen, Zuschneiden-Platzhalter, EXIF entfernen).

- **Async-Thumbnails:** Worker-Pool (Kerne−1, eigene COM-/WIC-Instanz); der UI-Thread meldet bei jedem Scroll die gewünschte Liste (Vorschau > sichtbar > Vorlauf), die Warteschlange wird ersetzt. JPEGs werden per `IWICBitmapSourceTransform` direkt DCT-verkleinert, EXIF-Orientation wird berücksichtigt.
- **EXIF-/GPS-Cleaner:** JPEG ohne Rotations-Tag verlustfrei (APP1/Exif/XMP, APP3–13/15 und Kommentare werden auf Byte-Ebene entfernt, ICC/JFIF bleiben); JPEG mit Rotations-Tag, PNG, TIFF, JXR per WIC-Re-Encode (JPEG Q95, Drehung eingebrannt). BMP/GIF/WebP werden nicht unterstützt. Einzelbild: Kopie `*_clean.*` oder Original überschreiben; Stapel: Kopien in `bereinigt/`.
- **Stand:** Auf Windows noch **nicht gebaut oder getestet** (MinGW-Syntaxcheck der Quellen lief durch). „Zuschneiden“ ist Platzhalter, „Vollbild“ schaltet nur das Fenster um, Einstellungen werden noch nicht gespeichert, Ordnerbaum/-listing laufen synchron im UI-Thread. Noch nicht im Suite-Launcher und im Installer eingetragen.

## Duncan's Driver Updater

Hardware- & Treiber-Manager für Windows. Liest alle Geräte samt Treiberdaten
aus, gruppiert sie, erkennt Updates und kann Treiber-Updates über Windows Update installieren.

### Funktionen

- **Geräte-Scan** über die Windows SetupAPI (Name, Kategorie, Version, Datum, Anbieter, Hardware-ID, Problemcode), asynchron im Hintergrund
- **Gruppierte Ansicht** (ausklappbar): Grafik & Monitor, Audio, Netzwerk & Bluetooth, Peripherie, Speicher, System & Prozessor, Sonstige
- **Update-Erkennung**
  - über den Windows Update Agent (Treiber-Updates, per Hardware-ID abgeglichen)
  - **Hersteller-Abgleich** für Grafikkarten (NVIDIA `VEN_10DE`, AMD `VEN_1002`, Intel `VEN_8086`) anhand von `vendor_releases.json`
- **Aktualisieren-Button** pro Karte: Windows-Update-Treiber werden mit Wiederherstellungspunkt installiert, bei Hersteller-Treibern öffnet sich die offizielle Download-Seite
- Suche, Filter (Alle / Updates / Probleme), Sortierung, CSV-Export, Treiber sichern (`pnputil /export-driver`), Hardware-ID kopieren
- Eigene Dark-Dialoge und Toasts, Banner mit Start-Animation

Die App benötigt Administratorrechte (UAC-Abfrage beim Start), weil Treiber-Operationen sie erfordern.

**Einstellungen (Zahnrad rechts neben „Scan starten“):** *Beim Start automatisch scannen* und *Start-Animation anzeigen*.
Gespeichert in `%LOCALAPPDATA%\DuncansDriverUpdater\settings.ini`. Einen Autostart mit Windows gibt es hier bewusst nicht, weil die App Administratorrechte verlangt.

## Duncan's Suite (Launcher)

`DuncansSuite.exe` ist ein kleines Hauptmenü im selben Dark-Stil: drei Karten starten **Duncan's Tidy** (`DuncansTidy.exe`),
den **Driver Updater** (`DuncansDriverUpdater.exe`) und den **PDF Reader** (`DuncansPdf.exe`). Gesucht wird zuerst im Ordner der Suite, für Tidy zusätzlich in
`%LOCALAPPDATA%\DuncansTidy`. Ist Tidy nicht installiert, zeigt die Karte „Nicht installiert“ und den Button **Herunterladen**: Ein Klick lädt die neueste `DuncansTidy.exe` per HTTPS von den GitHub-Releases (`Neox365/Duncans-Tidy-ToolSet`) nach `%LOCALAPPDATA%\DuncansTidy`, prüft die SHA-256-Summe aus `DuncansTidy.exe.sha256` und schaltet den Button danach auf „Starten“. Fehlt eine andere EXE, erscheint ein Dark-Dialog mit den durchsuchten Pfaden. Der Footer zeigt die Version
und öffnet mit „Tools-Ordner öffnen“ den Installationsordner. Der Banner zeigt einen leuchtenden Datenstrom in den Farben der drei Programme (Gold = Tidy, Cyan = Driver Updater, Magenta = PDF Reader); beim Start pulsiert er einmalig kurz (ca. 1,8 s, per Timer), danach bleibt er ruhig. Der Installer legt die Suite als Haupt-Verknüpfung auf den Desktop
und ins Startmenü.

### Automatische Updates der Suite

Beim Start fragt die Suite im Hintergrund die GitHub-Releases (`Neox365/Duncans-Suite`) ab. Gibt es einen neueren
`DuncansSuite_Setup_X.Y.Z.exe`, erscheint oben im Banner **„Update verfügbar (vX.Y.Z)“**. Ein Klick lädt den Installer nach
`%LOCALAPPDATA%\DuncansSuite\update` (mit Fortschritt; der Ordner ist einstellbar), prüft SHA-256 und Signatur, startet ihn (UAC) und beendet die Suite.
Es wird die Release-**Liste** abgefragt statt `/releases/latest`, weil `latest` Pre-Releases ignoriert. Ist man offline
oder GitHub nicht erreichbar, bleibt die Prüfung still.

### Einstellungen der Suite (Zahnrad im Banner)

Ein dunkler Dialog mit *Speichern* / *Abbrechen*; gespeichert in `%LOCALAPPDATA%\DuncansSuite\settings.ini`:

Der Dialog hat zwei Spalten: links **Allgemein** (Schalter), rechts **Pfade & Backups** sowie **Updates & Info**.

- **Vor kritischen Aktionen Wiederherstellungspunkt anlegen** (Standard: an; `AutoWiederherstellungspunkt` in der INI). Privacy Shield und Driver Updater lesen diese Einstellung und legen den Punkt „Duncan's Suite Pre-Optimization“ nur an, wenn sie an ist. Der manuelle Knopf im Privacy Shield ist davon unabhängig.
- **Pfade & Backups:** zeigt den Backup-Ordner (`%ProgramData%\DuncansSuite\Backups\`) mit *Ordner öffnen* und die Logdatei (`duncans_suite.log`) mit *Log-Datei anzeigen*.
- **Updates & Info:** *Nach Updates suchen* prüft sofort und meldet das Ergebnis in der Fußzeile („ist aktuell“, „Update verfügbar“ oder „offline?“); daneben stehen Version und Copyright.
- **Beim Start nach Updates suchen** (Standard: an)
- **Mit Windows starten** (Eintrag unter `HKCU\...\Run`, nur für das eigene Konto)
- **Download-Ordner für Update-Installer** (Standard: `%LOCALAPPDATA%\DuncansSuite\update`, nur absolute Pfade)
- **Beim Schließen in den Infobereich minimieren** (das X versteckt die Suite, statt sie zu beenden) und **Nach dem Start eines Programms minimieren** (beide Standard: aus)

- **Explorer-Kontextmenü für PDFs aktivieren:** Rechtsklick auf eine PDF → *Mit Duncan's PDF öffnen*
- **Explorer-Kontextmenü für Tidy-Analyse aktivieren:** Rechtsklick auf einen Ordner oder ein Laufwerk → *Mit Duncan's Tidy analysieren* (zeigt die 20 größten Dateien, es wird nichts gelöscht)

Die Kontextmenü-Einträge liegen unter `HKEY_CURRENT_USER\Software\Classes` (kein Administrator nötig) und werden beim Ausschalten wieder entfernt. Die Suite frischt sie beim Start auf den aktuellen Programmpfad auf.
Der Tidy-Eintrag ruft `DuncansSuite.exe --tidy-analyze "<Ordner>"` auf; die Suite prüft, ob Tidy 1.1.4 oder neuer installiert ist, und startet es dann mit `--analyze` (ältere Versionen würden das Argument als Reinigungsmodus missverstehen und werden deshalb nie damit gestartet).

### Infobereich (Tray)

Solange die Suite läuft, zeigt der Infobereich neben der Uhr ihr Symbol (Klick öffnet das Fenster). Rechtsklick öffnet ein dunkles Menü:
*Duncan's Suite öffnen*, *RAM schnell leeren (Quick Clean)*, *Tidy starten*, *Beenden*. Quick Clean verkleinert den Arbeitsspeicher laufender Programme
(mit Administrator-Rechten zusätzlich die Standby-Liste) und meldet das Ergebnis als Windows-Benachrichtigung. Auch ein gefundenes Suite-Update wird so gemeldet, solange das Fenster versteckt ist.

## Hersteller-Tabelle (`vendor_releases.json`)

Die SetupAPI kennt nur den *installierten* Treiber. Welche Version der Hersteller zuletzt veröffentlicht hat,
steht in `resources/vendor_releases.json` (wird beim Build neben die EXE kopiert und vom Installer mitgeliefert):

```json
{ "vendor": "NVIDIA", "ven": "VEN_10DE", "name": "GeForce Game Ready Treiber 617.42",
  "version": "32.0.16.1742", "date": "2026-10-06", "url": "https://www.nvidia.com/en-us/geforce/drivers/" }
```

- `version` ist die **Windows-Treiberversion** (`a.b.c.d`, wie im Geräte-Manager). NVIDIA: Release `617.42` entspricht `32.0.16.1742`.
- Leere `version` = Eintrag inaktiv (AMD ist so vorbelegt, bis eine geprüfte Version eingetragen ist).
- Die Tabelle veraltet mit jedem neuen Hersteller-Release und muss gepflegt werden (siehe unten: Online-Tabelle).
- Verglichen wird nur innerhalb desselben Treiber-Hauptzweigs (z. B. `32.0.x`), um falschen Alarm bei älteren GPU-Generationen zu vermeiden.

### Online-Tabelle (ohne neues Release aktualisieren)

Der Driver Updater holt beim Start im Hintergrund `resources/vendor_releases.json` samt `vendor_releases.json.sig` aus diesem Repository (GitHub `raw`, Zweig `main`).
Nur wenn die Signatur (Suite-Schlüssel, wie bei den Downloads) stimmt, wird die Tabelle in `%LOCALAPPDATA%\DuncansDriverUpdater\` zwischengespeichert; beim Laden wird sie erneut geprüft.
Offline oder bei einem Fehler bleibt es still und es gilt der bisherige Stand. Quellen und Vorrang: eingebaute Werte → Online-Tabelle → Datei neben der EXE; je Hersteller gewinnt der Eintrag mit dem **neueren `date`** (bei gleichem Datum der spätere).
Eine Änderung wirkt ab dem nächsten Start der App.

Pflege: `resources/vendor_releases.json` anpassen (`version`, `date`, `name`), dann

```powershell
powershell -File tools\SignVendorTable.ps1
git add resources/vendor_releases.json resources/vendor_releases.json.sig; git commit; git push
```

`SignVendorTable.ps1` bringt die Datei auf LF/UTF-8 ohne BOM (genau diese Bytes liefert GitHub aus) und erzeugt die `.sig`.

## Duncan's Privacy Shield

Datenschutz- & Telemetrie-Manager (`DuncansPrivacy.exe`, vierte Karte der Suite, eigene Programmversion 0.6.0). Läuft mit Administratorrechten (UAC), weil Richtlinien unter `HKLM` geschrieben werden.
Jede Einstellung ist ein Schalter und besteht aus klar benannten Registry-Werten (siehe `privacy/PrivacyRules.cpp`, `AllSettings()`):

| Gruppe | Einstellung | Registry (Auszug) |
| --- | --- | --- |
| Telemetrie & Diagnose | Telemetrie minimieren | `Policies\...\DataCollection\AllowTelemetry = 0` |
| | Feedback-Aufforderungen aus | `DoNotShowFeedbackNotifications = 1`, `Siuf\Rules` |
| | Telemetrie-Dienste stoppen | Dienste `DiagTrack`, `dmwappushservice` (Start = 4, werden beendet) |
| | Maßgeschneiderte Erfahrungen aus | `Privacy\TailoredExperiencesWithDiagnosticDataEnabled = 0` |
| Startmenü-Suche | Web-/Bing-Ergebnisse aus | `Search\BingSearchEnabled = 0`, `DisableSearchBoxSuggestions = 1` |
| System-Tracking | Werbe-ID aus | `AdvertisingInfo\Enabled = 0`, Richtlinie `DisabledByGroupPolicy` |
| | Standortdienste aus | `LocationAndSensors\DisableLocation = 1`, `ConsentStore\location = Deny` |
| | Eingabe-/Handschriftdaten nicht übermitteln | `Input\TIPC\Enabled = 0`, `InputPersonalization\*` |

**Sicherheit:** Vor der ersten Änderung wird ein Windows-Wiederherstellungspunkt erstellt (im Hintergrund; ist der Systemschutz aus, fragt das Programm nach, ob es trotzdem weitermachen soll).
Zusätzlich sichert das Programm vor dem Schreiben den Originalzustand jedes Wertes (vorhanden + Wert oder „nicht vorhanden“) in `%ProgramData%\DuncansSuite\Backups\privacy_backup.ini` (eine ältere Sicherung unter `%LOCALAPPDATA%\DuncansPrivacyShield` wird beim ersten Start übernommen).
Der Wiederherstellungspunkt heißt „Duncan's Suite Pre-Optimization“ und wird auch vom Driver Updater vor Treiber-Installationen angelegt (Windows erlaubt standardmäßig einen Punkt pro 24 h).
Alle gesetzten und zurückgenommenen Schlüssel samt Ergebnis stehen in `%ProgramData%\DuncansSuite\duncans_suite.log` (Privacy Shield, Driver Updater, Wiederherstellungspunkte; ab 1 MB Wechsel auf `.1`).
Ein einzelner Schalter nimmt genau seine Werte zurück, **Standard wiederherstellen** stellt alles auf den Zustand von vor der ersten Änderung. Schlägt ein Schreibvorgang mitten in einer Einstellung fehl, wird sie vollständig zurückgenommen.
Manche Richtlinien greifen erst nach Neustart oder Abmelden.

## Bauen

Voraussetzungen: CMake ≥ 3.20 und ein C++17-Compiler (MinGW-w64 oder MSVC), Windows 10/11.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Alle Programme landen in `release\` (`DuncansSuite.exe`, `DuncansDriverUpdater.exe`, `DuncansPdf.exe`); `vendor_releases.json` und `pdfium.dll` werden daneben kopiert.
Mit MinGW wird statisch gelinkt, es sind keine zusätzlichen DLLs nötig.

## Installer

[Inno Setup 6](https://jrsoftware.org/isinfo.php) installieren, dann nach dem Build:

```powershell
ISCC.exe installer\installer.iss
```

Ergebnis: `release\DuncansSuite_Setup_1.1.24.exe` (schlanker Admin-Installer: nur die Suite, Startmenü, optionale Desktop-Verknüpfung, Deinstaller). Die Programme lädt die Suite per „Herunterladen“ nach `%LOCALAPPDATA%` (Signaturprüfung) und hält sie einzeln aktuell. Komplett-Installer mit allen Programmen: `ISCC /DFull=1 installer\installer.iss` → `DuncansSuite_Full_Setup_1.1.24.exe`.

## Projektstruktur

| Pfad | Inhalt |
| --- | --- |
| `src/HardwareScanner.*` | SetupAPI-Scan (asynchron) |
| `src/UpdateChecker.*`, `src/UpdateInstaller.*`, `src/Dispatch.h` | Windows Update Agent (Suche / Installation, COM-Automation) |
| `src/VendorReleases.*` | Hersteller-Abgleich der Grafiktreiber |
| `src/MainWindow.*`, `src/Icons.*` | UI (Win32 + GDI/GDI+), Banner, Splash, Dialoge |
| `suite/main.cpp` | Duncan's Suite (Launcher) |
| `pdf/` | Duncan's PDF Reader (`main.cpp` UI, `PdfDocument.*` PDFium-Wrapper, `PdfiumApi.h`) |
| `privacy/` | Duncan's Privacy Shield (`main.cpp` UI, `PrivacyRules.*` Registry-Logik mit Sicherung) |
| `tools/` | `MakeSigningKey.ps1`, `SignRelease.ps1` |
| `release/` | Fertige Programme, Installer und `.sig`-Dateien (nicht im Repository) |
| `_archiv/` | Alte Installer (nicht im Repository) |
| `resources/` | Icons, Manifeste (Driver Updater: requireAdministrator, Suite: asInvoker), Versionsinfo, `vendor_releases.json` |
| `installer/installer.iss` | Inno-Setup-Skript |

## Hinweise

- Die Installation von Windows-Update-Treibern wurde bisher nicht mit einem echten Update getestet.
- Es gibt keinen automatischen Download von Hersteller-Treibern; dafür wird die Download-Seite geöffnet.

## Duncan's PDF Reader

Nativer PDF-Betrachter (`pdf/`, Ziel `DuncansPdfReader` → `DuncansPdf.exe`) im selben Dark-Stil: Werkzeugleiste oben, Seitenleiste links, Canvas mit Scrollen,
`Strg+Mausrad`-Zoom und Drag & Drop.

- **Seitenleiste:** Vorschaubilder aller Seiten; Klick wählt, `Strg`-/`Umschalt`-Klick wählt mehrere. Drehen und Löschen wirken auf die Auswahl (Werkzeugleiste, Tab „Werkzeuge“ oder Rechtsklick-Menü).
- **Merge:** Ein Dialog, in dem sich eine oder mehrere PDFs auswählen und hinten anhängen lassen.
- **Suche:** `Strg+F` öffnet ein Suchfeld oben rechts, Treffer werden auf der Seite markiert (aktueller Treffer kräftiger), Anzeige „x / y“.
- **Seiten umordnen:** Vorschauen in der Seitenleiste per Drag & Drop verschieben (auch mehrere ausgewählte Seiten zusammen); eine Linie zeigt die Einfügeposition.
- **Anmerkungen:** *Marker* (`H`) hebt per Maus überstrichenen Text gelb hervor, *Notiz* (`N`) setzt an der angeklickten Stelle eine Notiz mit Text. Beides wird als echte PDF-Anmerkung gespeichert; `Esc` beendet das Werkzeug.
- **Zuschnitt:** *Zuschnitt* (`C`) zieht einen Rahmen auf der Seite auf; nach der Bestätigung wird der sichtbare Bereich (CropBox) der ausgewählten Seiten (sonst der aktuellen; `Strg+A` in der Seitenleiste wählt alle) darauf gesetzt. Der Seiteninhalt bleibt in der Datei erhalten, `U` hebt den Zuschnitt wieder auf.
- **Formulare ausfüllen:** PDFs mit AcroForm-Feldern werden erkannt (Felder leicht blau hinterlegt). Textfelder, Kontrollkästchen, Optionsfelder, Auswahllisten und Schaltflächen lassen sich direkt anklicken und ausfüllen (Tastatur, `Strg+C/X/V/A`, `Esc` verlässt das Feld). Die Eingaben werden beim Speichern in die Datei geschrieben. XFA-Formulare werden nicht unterstützt.
- **Datei verkleinern** (Tab „Werkzeuge“): Bilder werden auf höchstens 150 dpi heruntergerechnet und mit JPEG-Qualität 70 neu kodiert, nur benutzte Objekte kommen in die neue Datei. Lesezeichen und Formularfelder gehen dabei verloren; das geöffnete Dokument bleibt unverändert.
- **Speichern:** schreibt eine Kopie mit allen Änderungen (Drehungen, gelöschte und angehängte Seiten); das Original bleibt unverändert.

| Kürzel | Aktion |
| --- | --- |
| `Strg+O` / `Strg+S` | Öffnen / Speichern unter |
| `Strg+F`, `Enter` / `Umschalt+Enter` oder `F3` / `Umschalt+F3` | Suchen, nächster / vorheriger Treffer; `Esc` schließt die Suche |
| `Bild auf` / `Bild ab`, `Pos1` / `Ende` | Seiten blättern |
| `Entf`, `R` / `Umschalt+R` | ausgewählte Seiten löschen / drehen |
| `H` / `N` | Marker- bzw. Notiz-Werkzeug ein-/ausschalten |
| `C` / `U` | Zuschnitt-Werkzeug ein-/ausschalten / Zuschnitt aufheben |
| `Strg+A`, `Strg+M`, `Strg+B` | alle Seiten wählen, Merge, Seitenleiste ein-/ausblenden |
| `Strg+Plus` / `Strg+Minus`, `Strg+0`, `Strg+1` | Zoom, Seitenbreite, 100 % |

Gerendert wird mit **PDFium**, das zur Laufzeit aus `pdfium.dll` geladen wird (64-Bit). Die DLL besorgt `pdf\third_party\get_pdfium.ps1`
(offizielle Builds von bblanchon/pdfium-binaries) und legt sie nach `pdf\third_party` (CMake kopiert sie beim Build nach `release\`). Ohne DLL startet die App und meldet es per Hinweis.

## Signierte Downloads (Sicherheit)

Die Suite führt nur Dateien aus, deren Signatur (ECDSA P-256 / SHA-256, `<Datei>.sig`) zum einkompilierten öffentlichen Schlüssel passt.
Eine SHA-256-Summe allein reicht nicht, weil sie vom selben Server kommt wie die Datei.

| Datei | Geprüft mit |
| --- | --- |
| `DuncansSuite_Setup_X.Y.Z.exe` (Suite-Update) | Suite-Schlüssel (`suite/public_key.inc`) |
| `DuncansPdf.exe` (Duncans-PDF-Reader-Release) | Suite-Schlüssel |
| `DuncansTidy.exe` | Tidy-Schlüssel (`suite/tidy_public_key.inc`, Kopie aus dem Tidy-Projekt) |

Einmalig: `powershell -File tools\MakeSigningKey.ps1` erzeugt den privaten Schlüssel in `%USERPROFILE%\DuncansSuite-Signing\` (sichern, nie hochladen)
und schreibt `suite/public_key.inc`; danach neu bauen.
Pro Release: Installer bauen, `powershell -File tools\SignRelease.ps1 release\<Datei>` ausführen und die erzeugte `.sig` als zusätzliches Asset ins selbe GitHub-Release laden.
Fehlt die `.sig`, lehnt die Suite die Datei ab.
