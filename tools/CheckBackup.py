"""Gegenprobe der ZIP-Dateien aus Sicherung und Steuerberater-Export mit dem unabhaengigen Python-Modul zipfile
(Pruefsummen, Namen mit Umlauten, Inhalt).
Aufruf:  python tools/CheckBackup.py ordner   (Ordner mit sicherung.zip und steuer.zip: docs/tests/BackupTest.exe ordner)"""
import os
import sys
import zipfile

d = sys.argv[1]
z = zipfile.ZipFile(os.path.join(d, "sicherung.zip"))
assert z.testzip() is None
names = set(z.namelist())
for must in (".oggys-index.tsv", ".kunden.tsv", ".rechnungsdaten/RE-2026-001.dat", "Protokoll.log", "LIESMICH.txt", "2026/Muster GmbH/Rechnung_RE-2026-001_Müller.pdf"):
    assert must in names, must
print("sicherung.zip in Ordnung:", len(names), "Dateien")
t = zipfile.ZipFile(os.path.join(d, "steuer.zip"))
assert t.testzip() is None
tn = set(t.namelist())
assert "Belegliste_2026.csv" in tn and "LIESMICH.txt" in tn and "Belege/2026/Muster GmbH/Rechnung_RE-2026-001_Müller.pdf" in tn
assert t.read("Belegliste_2026.csv").startswith(b"\xef\xbb\xbf")
print("steuer.zip in Ordnung:", len(tn), "Dateien")
