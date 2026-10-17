#pragma once
// VendorReleases.h - Abgleich der installierten Grafiktreiber mit bekannten Hersteller-Releases.
//
// Die Windows-SetupAPI kennt nur den *installierten* Treiber, nicht ob der Hersteller (NVIDIA, AMD,
// Intel) schon etwas Neueres veroeffentlicht hat. Windows Update liefert Hersteller-Treiber oft mit
// Wochen Verzug. Deshalb gibt es hier eine kleine Tabelle "neuester bekannter Release je Vendor":
//
//   * eingebaute Standardwerte (Stand siehe kBuiltInStamp in VendorReleases.cpp)
//   * optional vendor_releases.json neben der EXE - ueberschreibt/ergaenzt die Standardwerte, so
//     laesst sich die Tabelle ohne Neukompilieren aktualisieren.
//
// Vergleich: Windows-Treiberversion (a.b.c.d, numerisch). Ist sie nicht lesbar, entscheidet das Datum.

#include <string>
#include <vector>

#include "DriverInfo.h"

struct VendorRelease
{
    std::wstring vendor;    // Anzeigename, z. B. "NVIDIA"
    std::wstring venId;     // PCI-Hersteller-ID, GROSSBUCHSTABEN, z. B. "VEN_10DE"
    std::wstring name;      // z. B. "GeForce Game Ready Treiber 617.42"
    std::wstring version;   // Windows-Treiberversion, z. B. "32.0.16.1742" (leer = Eintrag inaktiv)
    std::wstring dateText;  // TT.MM.JJJJ (aus "JJJJ-MM-TT")
    int dateKey = 0;        // JJJJMMTT
    std::wstring url;       // Download-Seite des Herstellers
};

// Standardwerte + vendor_releases.json (falls vorhanden).
std::vector<VendorRelease> LoadVendorReleases();

// Markiert Grafikkarten mit aelterem Treiber als UpdateAvailable. Gibt die Anzahl zurueck.
// Geraete, die bereits ein Windows-Update-Angebot haben (updateId gesetzt), bleiben unveraendert.
int ApplyVendorReleases(std::vector<DriverInfo>& drivers, const std::vector<VendorRelease>& releases);
