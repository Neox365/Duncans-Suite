# Duncan's Suite

Windows-Programmsammlung in C++ / Win32 mit einheitlichem Dark-UI, Sprache der Programme: Deutsch.

| Programm | Aufgabe |
| --- | --- |
| **Duncan's Suite** | Launcher: startet die Programme, lädt fehlende per Ein-Klick-Download und aktualisiert sich selbst |
| **Duncan's Driver Updater** | Hardware- und Treiber-Manager (Scan, Update-Erkennung, Installation über Windows Update) |
| **Duncan's PDF Reader** | PDF-Betrachter und -Werkzeug (PDFium) |
| **Duncan's Photo Commander** | Foto-Verwaltung (Ordnerbaum, asynchrones Thumbnail-Grid, EXIF-/GPS-Cleaner), Quellen in `photo/` – Grundgerüst |
| **Oggys Docs-Files** | Rechnungs-Manager und Rechnungs-Generator (Ordner Jahr/Gläubiger, Status-Badges, PDF-Erzeugung), Quellen in `docs/` |
| **Duncan's Vault** | Lokaler Tresor für Passwörter, Lizenzen, WLAN-Schlüssel und Notizen (AES-256-GCM), Quellen in `vault/` |
| **Duncan's Tidy** | System-Cleaner und Optimierer, eigenes Projekt ([Neox365/Duncans-Tidy-ToolSet](https://github.com/Neox365/Duncans-Tidy-ToolSet)); die Suite lädt es nur herunter und startet es |

## Duncan's Photo Commander

Win32 + Direct2D/DirectWrite + WIC in `photo/`, Ziel `DuncansPhotoCommander.exe` (Farben aus `common/Theme.h` über `photo/Theme.h`).
Topbar (Ordner öffnen, Vollbild, Stapelverarbeitung, Zahnrad), links Ordnerbaum, Mitte Thumbnail-Grid, rechts Detail-Panel (EXIF, Auflösung, Dateigröße) mit Schnellaktionen.
Start mit Ordner oder Bild als Argument (`DuncansPhotoCommander.exe "C:\Fotos\a.jpg"`) oder per Drag & Drop auf das Fenster.

- **Thumbnail-Grid:** asynchrones WIC-Laden (Worker-Pool, eigene COM-/WIC-Instanz je Thread; die Warteschlange wird bei jedem Scroll ersetzt, JPEGs werden per DCT-Skalierung verkleinert, EXIF-Drehung wird berücksichtigt). Das Ordnerlisting läuft im Hintergrund. Mehrfachauswahl mit Strg/Umschalt-Klick, Umschalt+Pfeile und Strg+A; Sortierung nach Name, Datum oder Größe (auf-/absteigend); Entf verschiebt Bilder in den Papierkorb.
- **Bildbetrachter:** Doppelklick oder Enter (Vollbild-Knopf mit gewähltem Bild öffnet ihn im Vollbild). Mausrad zoomt zum Mauszeiger, Ziehen verschiebt, Doppelklick wechselt zwischen Einpassen und 100 %; ←/→ blättern, `0` einpassen, `1` 100 %, `+`/`-` Zoom, `R` / Umschalt+`R` drehen, `C` zuschneiden, Entf löschen, Esc schließt. Das Bild wird bis 5000 px Kantenlänge geladen (100 % bezieht sich auf die Originalgröße).
- **Zuschneiden:** Ausschnitt im Betrachter aufziehen, Enter speichert als Kopie `*_crop.*` (oder überschreibt das Original, siehe Zahnrad). Re-Encode (JPEG Q95), Metadaten bleiben erhalten.
- **EXIF-/GPS-Cleaner:** JPEG ohne Rotations-Tag und WebP verlustfrei auf Byte-Ebene (JPEG: APP1/Exif/XMP, APP3–13/15 und Kommentare; WebP: EXIF-/XMP-Chunks; ICC-Profil bleibt). JPEG mit Rotations-Tag, PNG, TIFF, JXR per WIC-Re-Encode (JPEG Q95, Drehung eingebrannt); BMP hat keine Metadaten und wird kopiert; GIF/HEIC werden übersprungen. Nach jedem Bereinigen prüft das Programm, ob die Standortdaten wirklich weg sind.
- **Stapelverarbeitung** (Menü am Knopf, gilt für die Auswahl oder alle Bilder des Ordners, mit Fortschritt in der Statusleiste und Abbruch per Esc): EXIF/GPS entfernen (Kopien im Ordner `bereinigt` oder Originale überschreiben), fürs Teilen verkleinern (längste Kante 2048 px, JPEG Q85, ohne Metadaten, Ordner `verkleinert`), alle 90° drehen. Das Ergebnis nennt, wie viele Bilder Standortdaten enthielten, übersprungen wurden oder fehlschlugen.
- **Aufnahmedatum & Standort-Filter:** Nach dem Ordnerlisting liest ein Hintergrundthread Aufnahmezeit und GPS-Flag aller Bilder. Sortierung „nach Aufnahmedatum (EXIF)“ (ohne EXIF-Zeit gilt das Änderungsdatum); Bilder mit Standortdaten tragen ein rotes GPS-Zeichen, der Zahnrad-Eintrag „Nur Bilder mit Standortdaten zeigen“ blendet alle anderen aus – zusammen mit Strg+A und dem Stapel „EXIF/GPS entfernen“ lassen sich so alle betroffenen Fotos auf einmal bereinigen.
- **Doppelte Bilder:** Stapel-Menü → „Doppelte und ähnliche Bilder im Ordner suchen“. Erst gleiche Dateigröße + Inhaltshash (identisch), dann ein 64-Bit-Differenzhash der Helligkeitsstruktur plus ein Farbvergleich einer 9×8-Verkleinerung (ähnlich, z. B. verkleinerte Kopie; gleich aufgebaute Bilder in anderen Farben gelten nicht als ähnlich); Esc bricht ab. Die Duplikatansicht zeigt nur die Gruppen (blau „behalten“ = größte Datei, orange „identisch/ähnlich“) und wählt alle außer dem behaltenen Bild vor – Entf schiebt sie in den Papierkorb, Esc beendet die Ansicht. Getestet in `photo/tests/DuplicatesTest.cpp`.
- **Bild bearbeiten:** Knopf „Bearbeiten …“ im Detail-Panel oder `E` im Betrachter. Rechts erscheinen Regler für Helligkeit, Kontrast, Sättigung (je −100…+100) und „Horizont ausrichten“ (−45°…+45°, im Uhrzeigersinn, randlos zugeschnitten); die Vorschau ist live (Farbe auf einer 2000-px-Vorschau, Drehung als Direct2D-Matrix), Doppelklick setzt einen Regler zurück. „Speichern“ legt eine Kopie `*_edit.*` an (oder überschreibt das Original, siehe Zahnrad), „Speichern unter …“ lässt Namen und Ordner wählen (Format bleibt das des Originals); gerechnet wird aus dem Original mit derselben Funktion (`imgops::ApplyEdits`) wie in der Vorschau, Metadaten bleiben erhalten. Getestet in `photo/tests/EditTest.cpp`. Hinweis: `HwndRenderTarget` kennt keine Direct2D-Effekte, deshalb läuft die Pipeline auf der CPU statt im Effect Graph.
- **Diashow:** Knopf „Vollbild“ in der Topbar, `F5` oder `F` startet sie ab dem gewählten Bild im echten Vollbild (ohne Ränder und Panels, am Ordnerende geht es von vorn los). Standard 4 s pro Bild (Zahnrad: 2 / 4 / 6 / 10 s). Leertaste/`P` pausiert, ←/→ springen, Esc beendet und bringt dich zurück. Bei Mausbewegung blendet unten eine Steuerleiste (Zurück, Play/Pause, Weiter, Schließen) ein und nach 2 s wieder aus, der Mauszeiger verschwindet mit ihr. Im Betrachter gilt `F5`, dort bleibt `F` = Einpassen und der Vollbild-Knopf der unteren Leiste schaltet nur das Vollbild um.
- **Einstellungen:** Zahnrad: Thumbnail-Größe, Sortierung, Standort-Filter, „Original überschreiben“; gespeichert in `%LOCALAPPDATA%\DuncansPhotoCommander\settings.ini` (inklusive zuletzt geöffnetem Ordner).
- **Release:** Download/Update über das Release `modules` im Suite-Repo (`tools\PublishModule.ps1 -Module Photo`), Karte im Launcher, im Komplett-Installer enthalten.
- **Stand:** Der GitHub-Workflow `Windows-Build` baut alle Programme mit MSVC und testet die Byte-Bereinigung (`photo/tests`). HEIC/GIF werden nicht bearbeitet.

## Oggys Docs-Files

Rechnungs-Manager und Rechnungs-Generator (Win32 + Direct2D, Quellen in `docs/`, Ziel `OggysDocsFiles.exe`, gleiches dunkles Flat-Design). Start mit Dateien oder Ordnern als Argument (`OggysDocsFiles.exe "C:\Scan\Strom.pdf"`) legt sie direkt zum Erfassen an.

- **Ordnerstruktur:** Alle Belege liegen unter `<Rechnungsordner>\<Jahr>\<Gläubiger>\` (Standard: `Dokumente\Rechnungen`, im Zahnrad änderbar), z. B. `Rechnungen\2026\Netzanbieter\Strom Oktober.pdf`. Wurzel und aktuelles Jahr werden beim Start angelegt, Jahr- und Gläubiger-Ordner beim Erfassen. Links zeigt ein Baum Jahre und Gläubiger mit Anzahl und filtert die Liste.
- **Liste:** Dateiname, Gläubiger, Betrag, Fälligkeit und Status-Badge (Bezahlt grün, Offen rot, Ratenzahlung gelb, Fixkosten blau). Spaltenkopf sortiert (Standard: Fälligkeit, Belege ohne Datum zuletzt); überfällige offene Rechnungen sind rot markiert. Doppelklick/Enter öffnet die Datei, Rechtsklick: Öffnen, Im Explorer zeigen, Status setzen (auch für mehrere), Bearbeiten, Papierkorb. F2 bearbeitet, Entf löscht (Papierkorb), Strg+A wählt alle, F5 liest neu ein (auch beim Zurückwechseln ins Fenster). Die Statusleiste zeigt Anzahl, Summe der offenen Beträge und überfällige Rechnungen.
- **Drag & Drop:** PDFs, JPG/PNG oder ganze Ordner ins Fenster ziehen (oder „+ Hinzufügen“, Strg+O): pro Datei fragt ein Formular Gläubiger (mit Vorschlägen aus vorhandenen Ordnern), Jahr, Betrag, Fälligkeit, Tags und Status ab und kopiert die Datei an den passenden Platz (Zahnrad: stattdessen verschieben). Vorhandene Dateien werden nie überschrieben („Name (2).pdf“).
- **PDF Reader:** PDFs öffnen (Doppelklick, Enter, Rechtsklick, frisch erzeugte Rechnungen) im Duncan's PDF Reader, sobald `DuncansPdf.exe` neben der EXE oder in `%LOCALAPPDATA%\DuncansPdf` liegt (Zahnrad: „PDFs im Duncan's PDF Reader öffnen“ schaltet das ab); sonst und für Bilder öffnet das Standardprogramm.
- **Datumseingabe:** In den Datumsfeldern genügt es, Ziffern zu tippen: `15112026` wird beim Tippen zu `15.11.2026` (Punkte nach Tag und Monat, höchstens acht Ziffern); selbst getippte Punkte bleiben unangetastet, auch zweistellige Jahre (`15.11.26`) sind erlaubt.
- **Ratenzahlung:** Ist im Formular der Status „Ratenzahlung“ gewählt, erscheint das Feld „Rate / Fortschritt“ (z. B. „Rate 3 von 12“); der Text steht in der Liste neben dem Badge und ist durchsuchbar (7. Spalte der `.oggys-index.tsv`, ältere Indexdateien bleiben lesbar).
- **Suche:** Die Leiste oben (Strg+F) filtert in Echtzeit; jedes Wort muss in Dateiname, Gläubiger, Jahr, Tags, Notiz, Status, Betrag oder Fälligkeit vorkommen.
- **Rechnungs-Generator:** „Rechnung erstellen“ (Strg+N): Gläubiger (Absender) mit Adresse, Empfänger mit Adresse, Seite „Absender & Konto“ (Steuernummer/USt-IdNr., Kleinunternehmer, Inhaber, Registergericht, Telefon, E-Mail, Webseite, Bank, IBAN, BIC → dreispaltige PDF-Fußzeile auf jeder Seite), Schlusstext mit Platzhaltern `[Gesamtsumme]`/`[Fälligkeitsdatum]` und den Knöpfen „Beispiel laden“ / „Felder leeren“, Rechnungsnummer (leer = automatisch fortlaufend `RE-JJJJ-NNN`; der Zähler je Jahr steht in `settings.ini` und geht auch nach dem Löschen einer Rechnung nie zurück, eine doppelt eingegebene Nummer wird abgelehnt), Steuernummer / USt-IdNr. des Absenders, Checkbox „Kleinunternehmer nach § 19 UStG“ (keine USt-Berechnung, Hinweistext auf der PDF), Datum, Fälligkeit, USt-Satz, bis zu 10 Positionen (Menge × Einzelpreis, Summen live) und Zahlungshinweis. Daraus entsteht eine A4-PDF (Absender, Rechnungskopf, Empfängerfeld, Positionstabelle mit Seitenumbruch, Netto/USt/Gesamt, Zahlungshinweis, Fußzeile) direkt im passenden Ordner; sie wird als „Offen“ mit Betrag und Fälligkeit erfasst und geöffnet. Absenderdaten, Zahlungshinweis und USt-Satz merkt sich das Programm für die nächste Rechnung. Der PDF-Schreiber (`docs/PdfWriter.h`) kommt ohne Bibliothek aus (Standardschriften Helvetica, WinAnsi). Die Vorlage ist ein Entwurf: weitere Pflichtangaben (z. B. Leistungsdatum) trägt man im Zahlungshinweis ein oder prüft sie vor dem Versand; sie ersetzt keine Steuerberatung.
- **Datenschutz:** Alle Daten (Index, Einstellungen, Rechnungs-PDFs) liegen lokal auf dem Gerät; das Programm überträgt keine personenbezogenen Daten an externe Server (steht auch im Dialog „Über Oggys Docs-Files“).
- **Daten:** Betrag, Fälligkeit, Status, Tags und Notiz stehen in `.oggys-index.tsv` im Rechnungsordner (Textdatei, wandert mit dem Ordner; Schlüssel ist der relative Pfad), Einstellungen in `%LOCALAPPDATA%\OggysDocsFiles\settings.ini`. Dateien, die von außen in den Ordner gelegt werden, erscheinen automatisch als „Offen“ ohne Betrag.
- **Tests:** `docs/tests/DocsTest.cpp` prüft Beträge, Datum, Namen, Index, Ordner-Scan, Suche, Textfeld, PDF-Aufbau (xref) und Rechnungsberechnung.

## Duncan's Vault

Lokaler Passwort- und Lizenz-Manager (Win32 + Direct2D/DirectWrite in `vault/`, Ziel `DuncansVault.exe`, gleiche dunkle Optik, Textfelder aus `docs/TextEdit.h`).

- **Master-Passwort:** Beim ersten Start legst du es fest (mindestens 8 Zeichen, mit Stärke-Anzeige); danach entsperrt eine Maske den Tresor. Es gibt **keine Wiederherstellung**. Falsche Eingaben werden gebremst (1 bis 6 Sekunden Wartezeit).
- **Verschlüsselung (nur Windows-eigene CNG, kein Fremdcode):** AES-256 im GCM-Modus (authentifiziert, Manipulationen und falsche Passwörter werden erkannt), Schlüssel per PBKDF2-HMAC-SHA256 mit 600.000 Runden und zufälligem Salz, pro Speichern ein neuer Nonce. Dateiaufbau: `DVLT` | Version | Iterationen | Salz | Nonce | Chiffretext | Tag; der Kopf wird als zusätzliche Daten mit geprüft.
- **Local-First:** Alles liegt in `%LOCALAPPDATA%\DuncansVault\vault.dat` (daneben die vorherige Fassung als `vault.dat.bak`, geschrieben wird atomar über eine `.tmp`-Datei). Keine Cloud, kein Sync, keine Telemetrie, keine Netzwerkzugriffe.
- **Kategorien:** Passwörter & Accounts, Software-Lizenzen, WLAN-Schlüssel, Vertrauliche Notizen (mehrzeilige Notizfelder). Suche über Titel, Benutzernamen und Tags; Hinzufügen (Strg+N), Bearbeiten (F2/Enter), Löschen (Entf, mit Rückfrage).
- **Generator:** Länge 8 bis 64, Groß-/Kleinbuchstaben, Ziffern, Sonderzeichen (ohne leicht verwechselbare Zeichen), kryptografischer Zufall; Stärke-Anzeige in 5 Stufen.
- **Komfort und Sicherheit:** Ein-Klick-Kopieren von Benutzername, Passwort und Lizenzschlüssel (Strg+C kopiert das Geheimnis des gewählten Eintrags). Die Zwischenablage wird 30 Sekunden nach dem Kopieren eines Geheimnisses geleert und ist vom Windows-Zwischenablageverlauf und der Cloud-Zwischenablage ausgenommen. Automatische Sperre nach 5 Minuten Inaktivität (Zahnrad, abschaltbar), Sperren per Knopf oder Strg+L; beim Sperren werden Einträge und Schlüssel aus dem Speicher überschrieben. Passwortfelder lassen sich nicht kopieren. Zahnrad: Master-Passwort ändern, Tresor-Ordner öffnen.
- **Backup:** Zahnrad → „Backup exportieren (.vault)“ kopiert die verschlüsselte Tresor-Datei an einen Ort deiner Wahl (verschlüsselt mit dem Master-Passwort zum Zeitpunkt des Exports); „Backup importieren“ fragt dessen Passwort ab und führt die Einträge zusammen (neue werden hinzugefügt, bei gleicher Id gewinnt die neuere Fassung, nichts wird gelöscht).
- **Tests:** `vault/tests/VaultTest.cpp` (Windows) prüft den PBKDF2-Testvektor aus RFC 7914, Roundtrip, falsches Passwort, Manipulation, Sicherung, Passwortwechsel, Serialisierung, Suche, Generator und Stärke-Bewertung. Für Tests ersetzt die Umgebungsvariable `DUNCANSVAULT_DIR` den Datenordner.

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

`DuncansSuite.exe` ist ein kleines Hauptmenü im selben Dark-Stil: sieben Karten im 3×3-Raster starten **Duncan's Tidy** (`DuncansTidy.exe`),
den **Driver Updater** (`DuncansDriverUpdater.exe`), den **PDF Reader** (`DuncansPdf.exe`), den **Privacy Shield**, **Oggys Docs-Files** (`OggysDocsFiles.exe`), **Duncan's Vault** (`DuncansVault.exe`) und den **Photo Commander** (`DuncansPhotoCommander.exe`, Download und Update über das Release `modules` wie beim Privacy Shield). Gesucht wird zuerst im Ordner der Suite, für Tidy zusätzlich in
`%LOCALAPPDATA%\DuncansTidy`. Ist Tidy nicht installiert, zeigt die Karte „Nicht installiert“ und den Button **Herunterladen**: Ein Klick lädt die neueste `DuncansTidy.exe` per HTTPS von den GitHub-Releases (`Neox365/Duncans-Tidy-ToolSet`) nach `%LOCALAPPDATA%\DuncansTidy`, prüft die SHA-256-Summe aus `DuncansTidy.exe.sha256` und schaltet den Button danach auf „Starten“. Fehlt eine andere EXE, erscheint ein Dark-Dialog mit den durchsuchten Pfaden. Der Footer zeigt die Version
und öffnet mit „Tools-Ordner öffnen“ den Installationsordner. Der Banner zeigt einen leuchtenden Datenstrom in den Farben der drei Programme (Gold = Tidy, Cyan = Driver Updater, Magenta = PDF Reader); beim Start pulsiert er einmalig kurz (ca. 1,8 s, per Timer), danach bleibt er ruhig. Der Installer legt die Suite als Haupt-Verknüpfung auf den Desktop
und ins Startmenü.

### Automatische Updates der Suite

Beim Start fragt die Suite im Hintergrund die GitHub-Releases (`Neox365/Duncans-Suite`) ab. Gibt es einen neueren
`DuncansSuite_Setup_X.Y.Z.exe`, erscheint oben im Banner **„Update verfügbar (vX.Y.Z)“**. Ein Klick lädt den Installer nach
`%LOCALAPPDATA%\DuncansSuite\update` (mit Fortschritt; der Ordner ist einstellbar), prüft SHA-256 und Signatur, startet ihn (UAC) und beendet die Suite.
Es wird die Release-**Liste** abgefragt statt `/releases/latest`, weil `latest` Pre-Releases ignoriert. Ist man offline
oder GitHub nicht erreichbar, bleibt die Prüfung still.

### Info-Dialog der Suite (Button „i“ links neben dem Zahnrad)

Zeigt die Neuigkeiten aller Programme (Tidy, Driver Updater, PDF Reader, Privacy Shield, Photo Commander und der Suite selbst), je ein Reiter pro Programm, neueste Einträge zuerst; Einträge, die jünger sind als die installierte Version, tragen ein grünes „NEU“, ein grüner Punkt am Reiter und am „i“ zeigt ein bereitstehendes Update. Quelle ist `changelog.txt` im Wurzelordner: die Suite bringt eine Kopie als Ressource mit (funktioniert offline) und lädt beim Öffnen die aktuelle Fassung aus dem Release `modules` (`tools\PublishModule.ps1` lädt sie bei jedem Programm-Release hoch). Neue Einträge also einfach oben im jeweiligen Abschnitt von `changelog.txt` ergänzen (Format steht im Dateikopf); Parser-Test: `suite/tests/ChangelogTest.cpp`. Bedienung: Mausrad/Pfeiltasten scrollen, ←/→ wechseln den Reiter, Esc schließt.

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
