# Verfahrensdokumentation (Vorlage) – Rechnungsstellung und Belegablage mit Oggys Docs-Files

> **Wichtig vorab**
> - Das ist eine **Vorlage**, keine Rechtsberatung und keine Bescheinigung. Die GoBD kennen keine Zertifizierung; verantwortlich ist der Unternehmer.
> - Alles in `[eckigen Klammern]` ist von dir auszufüllen oder zu prüfen. Streich, was nicht zutrifft.
> - Lass das fertige Dokument **einmal von deinem Steuerberater lesen**. Fristen und Details ändern sich.
> - Die Beschreibung des Programms unten gibt den Stand **Docs-Files 0.15.0** wieder. Ändert sich die Software oder dein Ablauf, ändere auch dieses Dokument und hebe die alte Fassung auf (Datum und Grund der Änderung in Abschnitt 8).

---

## 1. Allgemeine Beschreibung

| | |
|---|---|
| Unternehmen | [Firma / Inhaber, Anschrift] |
| Steuernummer / USt-IdNr. | [Nummer] |
| Verantwortlich für das Verfahren | [Name, Funktion] |
| Stand dieser Dokumentation | [Datum], Version [1] |
| Geltungsbereich | Ausstellen, Ablegen und Aufbewahren von **Ausgangsrechnungen** sowie Ablegen eingehender Rechnungen und Belege |
| Gewinnermittlung | [Einnahmen-Überschuss-Rechnung / Bilanzierung] |
| Buchführung | [wer bucht: selbst / Steuerberater (Name)], Übergabe der Belege [monatlich / quartalsweise / jährlich] |
| Kleinunternehmer (§ 19 UStG) | [ja / nein] |

**Zweck des Verfahrens:** Rechnungen werden mit Oggys Docs-Files erstellt, als PDF gespeichert und mit den Rechnungsdaten als E-Rechnung (ZUGFeRD / Factur-X, EN 16931) versehen. Eingehende Rechnungen und Belege werden in demselben Ordner abgelegt und mit Betrag, Fälligkeit und Status erfasst.

## 2. Eingesetzte Software und Systeme

| Komponente | Beschreibung |
|---|---|
| Programm | Oggys Docs-Files (Teil von Duncan's Suite), Version [aktuelle Version eintragen] |
| Betriebssystem / Rechner | [Windows 11, Rechnername, wer hat Zugriff] |
| Speicherort der Belege | [Pfad des Rechnungsordners, z. B. D:\Rechnungen] |
| Datensicherung | [Ziel, z. B. externe Festplatte / Cloud-Speicher] |
| Weitere Software | [PDF-Reader, Buchhaltungs- oder Steuerprogramm, Banking-Software] |

Das Programm arbeitet lokal. Es überträgt keine Rechnungsdaten an externe Server. [Prüfen, ob dein Cloud-Speicher den Rechnungsordner synchronisiert; wenn ja, hier eintragen.]

## 3. Ablauf: Ausgangsrechnung

1. **Erstellen:** Im Programm „Rechnung erstellen“. Eingetragen werden Absender, Empfänger (aus der Kundenliste oder neu), Rechnungsdatum, Leistungsdatum oder Leistungszeitraum, Positionen mit Menge, Einzelpreis und Umsatzsteuersatz sowie der Zahlungshinweis.
2. **Rechnungsnummer:** Wird fortlaufend nach dem Muster `RE-JJJJ-NNN` vergeben (Zähler je Jahr; die Nummer wird auch bei vorhandenen Dateien nicht doppelt vergeben). Eine selbst eingetragene Nummer wird auf Doppelvergabe geprüft. [Falls du eigene Nummern vergibst: beschreibe das System und stelle sicher, dass es eindeutig ist.]
3. **Ergebnis:** Eine PDF-Datei mit Pflichtangaben nach § 14 UStG (soweit eingetragen) im Rechnungsordner unter `Jahr\Absender\`. Sind Adressen („PLZ Ort“ als letzte Zeile) und Steuernummer vorhanden, enthält die PDF als Anhang die Rechnungsdaten als E-Rechnung (`factur-x.xml`, Profil EN 16931).
4. **Sperre:** Jede so erstellte Rechnung wird im Programm **gesperrt**. Sie kann im Programm nicht gelöscht werden; Gläubiger, Jahr, Betrag und Fälligkeit sind nicht mehr änderbar. Das Programm speichert beim Erstellen eine SHA-256-Prüfsumme der PDF und meldet beim Start, wenn die Datei seither verändert wurde.
5. **Zahlungsstatus:** Offen / Bezahlt / Ratenzahlung / Fixkosten wird im Programm gepflegt. Das ändert die Rechnung selbst nicht.
6. **Korrektur / Storno:** Eine fehlerhafte Rechnung wird **nicht geändert oder gelöscht**, sondern über „Stornieren …“ mit einer Gutschrift mit eigener Nummer und Bezug auf die Rechnung korrigiert (als E-Rechnung Typ 381). Danach wird, falls nötig, eine neue Rechnung ausgestellt.
7. **Versand:** [Wie geht die Rechnung an den Kunden? E-Mail als PDF / Post / Portal. Bei E-Rechnungs-Pflicht im B2B: Absprache mit dem Kunden zum Format.]
8. **Zahlungseingang:** [Wie wird er geprüft, z. B. Kontoauszug abgleichen, wöchentlich; wer setzt den Status „Bezahlt“.]

**Beispielrechnungen** („Beispiel laden“) sind nicht gesperrt und dürfen nicht an Kunden gehen oder in die Buchführung gelangen.

## 4. Ablauf: Eingangsrechnungen und sonstige Belege

1. **Eingang:** [E-Mail-Postfach, Post, Portale]. Papierbelege werden [gescannt (Auflösung, Lesbarkeit, Zeitpunkt)] und das Original [aufbewahrt / nach Scan vernichtet – nur mit Verfahrensanweisung zum ersetzenden Scannen].
2. **Ablage:** Hinzufügen im Programm (Drag & Drop oder „+ Hinzufügen“). Die Datei wird in den Rechnungsordner kopiert [oder verschoben – Einstellung prüfen], Gläubiger, Jahr, Betrag und Fälligkeit werden erfasst; bei Scans und PDFs liest das Programm Vorschläge aus (Texterkennung) – **diese Werte sind zu kontrollieren**.
3. **E-Rechnungen (XRechnung, ZUGFeRD/Factur-X):** Das Programm liest Betrag, Datum und Absender aus dem XML. Es **prüft die E-Rechnung nicht** auf Gültigkeit. [Wer prüft eingehende E-Rechnungen formal und inhaltlich, mit welchem Werkzeug?]
4. **Unveränderbarkeit eingehender Belege:** Eingehende Belege sind im Programm **nicht gesperrt** (nur selbst erstellte Rechnungen sind es). [Maßnahme: z. B. Ordner nach Jahresabschluss auf schreibgeschützt setzen oder in das Steuerberater-Archiv übertragen.]

## 5. Datensicherung und Aufbewahrung

| Thema | Regelung |
|---|---|
| Datensicherung | Im Programm: Zahnrad → „Sicherung des Rechnungsordners erstellen (ZIP) …“. Rhythmus: [wöchentlich / nach jeder Rechnung / monatlich]. Ziel: [Datenträger/Ort, der vom Rechner getrennt ist]. Verantwortlich: [Name]. |
| Test der Rückspielung | [mindestens einmal jährlich eine Sicherung in einen leeren Ordner entpacken und im Programm öffnen; Datum eintragen] |
| Aufbewahrungsfrist | [Rechnungen und Buchungsbelege: derzeit **8 Jahre** nach § 147 AO / § 14b UStG, übrige Geschäftsunterlagen 6 bzw. 10 Jahre – bitte vom Steuerberater bestätigen lassen] ab Ende des Kalenderjahres der Entstehung |
| Lesbarkeit | Dateiformate: PDF (Rechnungen), XML (eingebettete E-Rechnung), CSV/TSV (Index, Belegliste), Textdatei (Protokoll). Bei Software-Wechsel bleibt der Ordner ohne das Programm lesbar. [Hinweis: Die PDFs sind **kein PDF/A**; für die Langzeitarchivierung prüfen, ob dein Steuerberater ein Archivformat verlangt.] |
| Datenzugriff der Finanzverwaltung (Z1–Z3) | Der Ordner liegt in einem offenen Format vor. Für eine Prüfung kann über Zahnrad → „Export für den Steuerberater“ pro Jahr eine ZIP mit Belegliste (CSV) und Belegen erzeugt werden. [Ansprechpartner bei einer Prüfung: Name] |
| Löschen nach Fristablauf | [Wer prüft jährlich, was gelöscht werden darf; Beachtung der DSGVO] |

## 6. Kontrollen und Nachweise (internes Kontrollsystem)

| Kontrolle | Wie im Verfahren umgesetzt |
|---|---|
| Vollständigkeit der Ausgangsrechnungen | Fortlaufende Nummern `RE-JJJJ-NNN`; Lücken fallen in der Liste auf. [Wer prüft, wie oft: ] |
| Keine nachträgliche Änderung | Sperre im Programm; SHA-256-Prüfsumme je Rechnung; Warnung in der Statuszeile beim Start, wenn eine gesperrte Rechnung außerhalb des Programms verändert wurde. |
| Änderungsprotokoll | Datei `Protokoll.log` im Rechnungsordner: Erstellen, Storno, Statusänderungen, abgelehntes Löschen, erkannte Veränderungen, jeweils mit Zeit. Jede Zeile enthält die Prüfsumme der vorigen Zeile (Hash-Kette); das Programm meldet beim Start, wenn die Kette nicht mehr stimmt. |
| Zugriffsschutz | [Windows-Benutzerkonto mit Passwort, Festplattenverschlüsselung, Bildschirmsperre, wer darf den Rechnungsordner öffnen] |
| Vier-Augen-Prinzip / Stichproben | [z. B. monatlicher Abgleich Liste ↔ Kontoauszug ↔ Buchführung durch …] |
| Prüfung der Pflichtangaben | [Checkliste § 14 Abs. 4 UStG vor dem ersten Versand: Name/Anschrift beider Parteien, Steuernummer/USt-IdNr., Ausstellungsdatum, Rechnungsnummer, Leistungsbeschreibung, Leistungszeitpunkt, Entgelt, Steuersatz und -betrag oder Hinweis auf Steuerbefreiung] |

**Bekannte Grenzen dieser Kontrollen (ehrlich festhalten):**
- Die Sperre gilt **nur im Programm**. Im Explorer lässt sich eine Datei weiterhin löschen oder ändern; das Programm bemerkt eine **Änderung** beim nächsten Start, ein **gelöschtes** Dokument meldet es **nicht**. [Kompensation: Sicherung, Zugriffsbeschränkung, Abgleich der Rechnungsnummern-Liste mit der Buchführung.]
- Das Protokoll ist ein Manipulationsnachweis, kein Schutz. Wer die Datei komplett neu schreibt oder die letzten Zeilen entfernt, wird nicht erkannt. [Kompensation: Sicherungen aufbewahren; Protokoll in der Sicherung vergleichen.]
- Rechnungen, die **vor** Version 0.12.0 erstellt wurden, sind nicht gesperrt und haben keine Prüfsumme.
- Die erzeugte E-Rechnung wurde gegen die Schemas (XSD) geprüft, **nicht** mit dem Prüftool der KoSIT validiert. Bei Pflichtempfängern vor dem Einsatz mit einem Prüfer testen. Keine XRechnung für Behörden (keine Leitweg-ID). Nur Rechnungen in Euro an Empfänger in Deutschland.

## 7. Technische Beschreibung (Systemdokumentation)

| Merkmal | Beschreibung |
|---|---|
| Datenablage | Ordner `[Rechnungsordner]\Jahr\Absender\Datei.pdf` |
| Index | `.oggys-index.tsv` (Textdatei, Tabulator-getrennt): je Beleg Pfad, Betrag, Fälligkeit, Status, Tags, Notiz, Art, Sperre, Prüfsumme, Bezug |
| Kundenliste | `.kunden.tsv` |
| Rechnungsdaten für Stornos | Ordner `.rechnungsdaten`, je Rechnung eine Datei `RE-JJJJ-NNN.dat` (Textdatei) |
| Änderungsprotokoll | `Protokoll.log` (Textdatei, eine Zeile je Ereignis, Hash-Kette SHA-256) |
| Einstellungen und Nummernzähler | `%LOCALAPPDATA%\OggysDocsFiles\settings.ini` (Zähler je Jahr; **mitsichern**, sonst beginnt die Nummerierung neu – Doppelvergabe wird aber gegen vorhandene Dateien geprüft) |
| Berechnung | Umsatzsteuer je Steuersatz auf die Nettosumme, kaufmännisch gerundet; Gutschrift (Storno) mit positiven Beträgen und Bezug auf die Rechnung |
| Programmänderungen | Neue Programmversionen werden über die Suite verteilt; Änderungen stehen im Änderungsprotokoll der Programme (`changelog.txt`). [Wer installiert Updates, wann, wie wird nach einem Update geprüft, dass Rechnungen weiter korrekt erstellt werden – z. B. Testrechnung mit „Beispiel laden“.] |

## 8. Änderungshistorie dieser Dokumentation

| Datum | Version | Änderung | Bearbeiter |
|---|---|---|---|
| [Datum] | 1 | Erstfassung | [Name] |

---

### Anlagen (nach Bedarf)

- [Beispielrechnung als PDF, mit Rechnungsnummer des Testlaufs]
- [Checkliste Monatsabschluss: Rechnungsnummern lückenlos? Status gepflegt? Sicherung gemacht? Export an den Steuerberater?]
- [Verfahrensanweisung ersetzendes Scannen, falls Papierbelege vernichtet werden]
- [Vertrag/Absprache mit dem Steuerberater über die Übergabe der Belege]
