#pragma once
// Duncan's Equalizer - Frequenzkurve und eigene Filter. Reine Rechenlogik ohne Oberflaeche (testbar).
// Die Kurve wird aus dem Konfigurationstext berechnet, den BuildConfig fuer Equalizer APO erzeugt: Sie zeigt also genau das,
// was auch geschrieben wird (Regler, Bass, Sprache und alle eingelesenen oder eigenen Filterzeilen). Equalizer APO rechnet intern
// mit eigenen Naeherungen (z. B. beim Graphic-EQ); die Kurve ist deshalb eine gute Orientierung, kein Messergebnis.
#include <string>
#include <vector>

#include "EqLogic.h"

namespace eq {

// Ein Filter in der Schreibweise von Equalizer APO: "Filter: ON PK Fc 1000 Hz Gain 3 dB Q 1.0"
struct Filter {
    std::string type = "PK";   // PK (Glocke), LSC (Tiefen-Shelf), HSC (Hoehen-Shelf), LP (Tiefpass), HP (Hochpass), NO (Kerbfilter)
    bool on = true;
    double fc = 1000;          // 20 ... 20000 Hz
    double gain = 0;           // dB (nur PK, LSC, HSC)
    double q = 0;              // 0 = Standardwert des Typs
};

constexpr double kMinFc = 20, kMaxFc = 20000, kMaxFilterGain = 24, kMinQ = 0.1, kMaxQ = 20;

bool FilterUsesGain(const std::string& type);   // PK, LSC, HSC
bool FilterUsesQ(const std::string& type);      // PK, NO (LSC/HSC: optional, hier nicht einstellbar)
const wchar_t* FilterTypeName(const std::string& type);   // "Glocke", "Tiefen-Shelf" ...
double DefaultQ(const std::string& type);

// Zeile lesen ("Filter: ON PK Fc 1000 Hz Gain 3 dB Q 1.0"); false bei Preamp-Zeilen, unbekannten Typen und Angaben, die hier nicht
// unterstuetzt sind (z. B. Bandbreite statt Q). Werte werden auf den erlaubten Bereich begrenzt.
bool ParseFilter(const std::string& line, Filter& f);
// Zeile schreiben (ASCII, nur harmlose Zeichen, passt durch die Pruefung von BuildConfig)
std::string FormatFilter(const Filter& f);

// n Frequenzen von lo bis hi, logarithmisch verteilt
std::vector<double> LogFreqs(int n, double lo = 20, double hi = 20000);

// Pegel in dB bei den gegebenen Frequenzen, aus dem Konfigurationstext (Preamp, GraphicEQ, Filter). Die Vorverstaerkung (Preamp)
// ist nur enthalten, wenn includePreamp gesetzt ist.
std::vector<double> ResponseDbFromConfig(const std::string& config, const std::vector<double>& freqs, bool includePreamp = false);
std::vector<double> ResponseDb(const Settings& s, const std::vector<double>& freqs, bool includePreamp = false);

}  // namespace eq
