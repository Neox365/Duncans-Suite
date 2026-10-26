#pragma once
// DeviceRepair.h - Versucht ein Geraet mit Problemcode (Geraete-Manager) selbst zu reparieren.
//
// Ablauf in Repair() (im Worker-Thread, benoetigt Administratorrechte):
//   1. Systemwiederherstellungspunkt (wenn in den Suite-Einstellungen aktiv)
//   2. je nach Code: aktivieren (22), Geraet neu starten, Bus neu einlesen, Geraet entfernen und neu erkennen lassen
//   3. nach jedem Schritt pruefen, ob das Problem verschwunden ist
// Linker-Bibliotheken: setupapi.lib, cfgmgr32.lib

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

#include "DriverInfo.h"

// WM_REPAIR_DONE: lParam = new devrepair::Result* (vom Empfaenger freizugeben)
constexpr UINT WM_REPAIR_DONE = WM_APP + 7;

namespace devrepair
{
    struct Result
    {
        std::wstring deviceName;
        bool fixed = false;
        bool rebootNeeded = false;
        std::wstring summary;              // eine Zeile: Ergebnis
        std::vector<std::wstring> steps;   // was versucht wurde (auch im Log)
    };

    // "Code 10 – Das Gerät kann nicht gestartet werden"
    std::wstring ProblemTitle(unsigned long code);
    // Kurzer Hinweis zur Ursache und was meist hilft
    std::wstring ProblemAdvice(unsigned long code);
    // false bei Codes, die Software nicht beheben kann (nicht angeschlossen, blockiert, Neustart noetig ...)
    bool CanTryRepair(unsigned long code);
    // Entfernen und neu erkennen ist nur fuer unkritische Geraete erlaubt (nicht Grafik, Speicher-Controller, Prozessor ...)
    bool MayReinstall(const DriverInfo& d);

    Result Repair(const DriverInfo& d);
}
