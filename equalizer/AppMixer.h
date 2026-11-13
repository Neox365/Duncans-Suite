#pragma once
// Duncan's Equalizer - Lautstaerke pro Programm (der Windows-Lautstaerkemixer). Nutzt die Windows-Audio-Sitzungen des
// Standard-Ausgabegeraets: jedes Programm, das gerade Ton ausgibt, hat eine eigene Lautstaerke (0 ... 100 %) und eine Stumm-Schaltung.
// Windows kennt keine Anhebung ueber 100 %: lauter wirkt ein Programm, indem die anderen leiser gestellt werden.
// Mehrere Sitzungen desselben Programms (z. B. Browser mit vielen Tabs) werden zu einer Zeile zusammengefasst.
#include <string>
#include <vector>

namespace mixer {

struct Entry {
    std::wstring key;      // eindeutig je Programm (kleingeschriebener Dateiname) oder "#system"
    std::wstring name;     // Anzeigename ("Chrome", "Systemklaenge")
    float volume = 1.f;    // 0 ... 1
    bool muted = false;
};

// Liest die aktuell Ton ausgebenden Programme (ohne dieses Programm selbst), nach Name sortiert. Leer, wenn nichts spielt
// oder Windows keine Auskunft gibt.
std::vector<Entry> List();

// Setzt Lautstaerke bzw. Stumm fuer alle Sitzungen des Programms mit diesem Schluessel. Rueckgabe: mindestens eine Sitzung erreicht.
bool SetVolume(const std::wstring& key, float volume);
bool SetMuted(const std::wstring& key, bool muted);

// Nur fuer Tests der Oberflaeche: DUNCANS_EQ_FAKEMIXER=1 liefert vier erfundene Programme statt der echten Sitzungen.
bool FakeMode();

// reine Hilfsfunktion (testbar): "C:\\Programme\\Google\\chrome.exe" -> "Chrome"
std::wstring DisplayNameFromPath(const std::wstring& path);

}  // namespace mixer
